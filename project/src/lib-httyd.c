// SPDX-License-Identifier: GPL-2.0+
// DreamWorks "How to Train Your Dragon" (Wii) formats -- see lib-httyd.h for
// exactly what is and is not understood about each of them.

#include "lib-httyd.h"
#include "lib-nintendo.h"
#include <zlib.h>
#include <string.h>
#include <ctype.h>

//-----------------------------------------------------------------------------
// (1) ".RWS" / ".mtd" chunk-tree container

enum
{
	HTTYD_RWS_HEADER_SIZE	= 0x0c,
	HTTYD_RWS_MARKER	= 0x1c020065,
	HTTYD_RWS_MAX_DEPTH	= 16, // safety stop for the recursive walk below
};

int IsHTTYDRws (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < HTTYD_RWS_HEADER_SIZE)
		return 0;

	const u32 type = le32 (data + 0x00);
	const u32 pay_size = le32 (data + 0x04);
	const u32 marker = le32 (data + 0x08);
	if (marker != HTTYD_RWS_MARKER)
		return 0;
	(void) type;

	// The outer chunk's payload must exactly reach EOF (confirmed true in
	// every real sample, both .RWS and .mtd), which is what tells this
	// container apart from arbitrary data that happens to contain the
	// marker constant somewhere.
	const u64 total = (u64) HTTYD_RWS_HEADER_SIZE + pay_size;
	if (file_size)
		return total == file_size;
	return total == size;
}

static void httyd_rws_walk
(
	FILE		*f,
	const u8	*data,
	size_t		size,
	size_t		off,
	size_t		end,
	int		depth
)
{
	while (off + HTTYD_RWS_HEADER_SIZE <= end)
	{
		const u32 type = le32 (data + off + 0x00);
		const u32 pay_size = le32 (data + off + 0x04);
		const u32 marker = le32 (data + off + 0x08);
		if (marker != HTTYD_RWS_MARKER)
		{
			fprintf (f, "%*s# @0x%zx: marker mismatch (0x%08x) -- stopping walk\n",
				depth * 2, "", off, marker);
			return;
		}

		const size_t pay_off = off + HTTYD_RWS_HEADER_SIZE;
		const size_t chunk_end = pay_off + pay_size;
		fprintf (f, "%*schunk @0x%zx: type=0x%x size=0x%x payload=0x%zx..0x%zx\n",
			depth * 2, "", off, type, pay_size, pay_off, chunk_end);

		if (chunk_end > end)
		{
			fprintf (f, "%*s# payload runs past enclosing chunk/EOF -- stopping walk\n",
				depth * 2, "");
			return;
		}

		// Recurse only when the payload itself begins with another chunk
		// header carrying the same marker and a size that fits inside
		// this chunk -- confirmed shape of every non-leaf chunk seen.
		if (depth < HTTYD_RWS_MAX_DEPTH
			&& pay_size >= HTTYD_RWS_HEADER_SIZE
			&& le32 (data + pay_off + 0x08) == HTTYD_RWS_MARKER
			&& (u64) le32 (data + pay_off + 0x04) + HTTYD_RWS_HEADER_SIZE <= pay_size )
		{
			httyd_rws_walk (f, data, size, pay_off, chunk_end, depth + 1);
		}
		else
		{
			// Leaf chunk: opportunistically show a short embedded ASCII
			// tag if one is present near the start of the payload (e.g.
			// the "Stream0" tag seen in real .RWS audio-bank leaves).
			size_t scan = pay_off;
			size_t scan_end = pay_size > 0x40 ? pay_off + 0x40 : chunk_end;
			while (scan < scan_end)
			{
				size_t start = scan;
				while (scan < scan_end && isprint (data[scan]))
					scan++;
				if (scan - start >= 4)
				{
					fprintf (f, "%*s  tag = \"%.*s\"\n", depth * 2, "",
						(int) (scan - start), data + start);
					break;
				}
				scan++;
			}
		}

		off = chunk_end;
	}
}

enumError DecodeHTTYDRws_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsHTTYDRws (data, size, file_size))
		return EINVAL;

	fprintf (f, "# DreamWorks How to Train Your Dragon .RWS/.mtd chunk-tree container\n");
	fprintf (f, "# 12-byte chunk headers: u32 type, u32 size, u32 marker(=0x%x), all little-endian\n",
		HTTYD_RWS_MARKER);
	fprintf (f, "# leaf payload contents (audio/DSP sample data) not reverse-engineered\n\n");

	httyd_rws_walk (f, data, size, 0, size, 0);
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".KRV" gzip-wrapped localization string table

int IsHTTYDKrv (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!data || size < 10)
		return 0;

	// Standard gzip magic + deflate method, as seen in every real sample.
	return data[0] == 0x1f && data[1] == 0x8b && data[2] == 0x08;
}

// Inflate a full gzip member into a heap buffer. Returns NULL on failure.
// Caller must FREE() the result.
static u8 * httyd_krv_inflate (const u8 *data, size_t size, size_t *out_size)
{
	z_stream strm;
	memset (&strm, 0, sizeof (strm));
	strm.next_in = (Bytef *) data;
	strm.avail_in = (uInt) size;

	if (inflateInit2 (&strm, 15 + 32) != Z_OK) // 32: auto-detect gzip/zlib header
		return 0;

	size_t cap = size * 4 + 0x1000;
	u8 *buf = MALLOC (cap);
	if (!buf)
	{
		inflateEnd (&strm);
		return 0;
	}

	bool ok = false;
	for (;;)
	{
		strm.next_out = buf + strm.total_out;
		strm.avail_out = (uInt) (cap - strm.total_out);

		int ret = inflate (&strm, Z_NO_FLUSH);
		if (ret == Z_STREAM_END)
		{
			ok = true;
			break;
		}
		if (ret != Z_OK)
			break;

		if (strm.avail_out == 0)
		{
			size_t used = strm.total_out;
			cap *= 2;
			u8 *n = REALLOC (buf, cap);
			if (!n)
				break;
			buf = n;
			(void) used;
		}
	}

	size_t total = strm.total_out;
	inflateEnd (&strm);

	if (!ok)
	{
		FREE (buf);
		return 0;
	}

	*out_size = total;
	return buf;
}

enumError DecodeHTTYDKrv_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsHTTYDKrv (data, size, file_size))
		return EINVAL;

	size_t inf_size = 0;
	u8 *inf = httyd_krv_inflate (data, size, &inf_size);
	if (!inf)
	{
		fprintf (f, "# gzip inflate failed -- nothing to decode\n");
		return ERR_OK;
	}

	fprintf (f, "# DreamWorks How to Train Your Dragon .KRV localization string table\n");
	fprintf (f, "# gzip-wrapped; decompressed size = %zu bytes\n", inf_size);

	if (inf_size >= 16)
	{
		fprintf (f, "header_u32[0] = 0x%x\n", le32 (inf + 0x00));
		fprintf (f, "header_u32[1] = 0x%x\n", le32 (inf + 0x04));
		fprintf (f, "header_u32[2] = 0x%x\n", le32 (inf + 0x08));
		fprintf (f, "# leading header fields not fully reverse-engineered -- see lib-httyd.h\n");
	}

	fprintf (f, "\n# UTF-16LE NUL-terminated strings found in the decompressed payload\n");

	uint index = 0;
	size_t i = 0;
	while (i + 1 < inf_size)
	{
		// Look for the start of a run of UTF-16LE text: an ASCII/Latin-1
		// printable low byte followed by a zero high byte.
		if (inf[i] < 0x20 || inf[i] > 0x7e || inf[i + 1] != 0)
		{
			i++;
			continue;
		}

		size_t start = i;
		size_t j = i;
		char utf8[2048];
		size_t ulen = 0;
		while (j + 1 < inf_size)
		{
			u16 c = inf[j] | (inf[j + 1] << 8);
			if (!c)
				break;
			if (c < 0x20 || c > 0x7e)
				break;
			if (ulen < sizeof (utf8) - 1)
				utf8[ulen++] = (char) c;
			j += 2;
		}

		if (ulen >= 2)
		{
			utf8[ulen] = 0;
			fprintf (f, "string[%u] @0x%zx = \"%s\"\n", index++, start, utf8);
			i = j + 2; // skip the NUL terminator too, if present
		}
		else
			i++;
	}

	FREE (inf);
	return ERR_OK;
}
