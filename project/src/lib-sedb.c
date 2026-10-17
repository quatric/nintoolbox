// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Square Enix SEDB container scanner and texture decoder; see lib-sedb.h.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-sedb.h"
#include "lib-gxdec.h"
#include <string.h>

#undef free // open_memstream() buffers belong to libc

#define SEDB_MAX_RECORDS 0x10000
#define SEDB_RES_HEADER 0x30
#define SEDB_DDS_OFFSET 0x20
#define SEDB_PIXELS_OFFSET 0xa0

static u32 sedb_le32 (const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

static u32 sedb_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

bool IsSEDB (const u8 *data, uint size)
{
	if (!data || size < 0x20 || memcmp (data, "SEDB", 4))
		return false;
	// PHB blobs carry a big-endian header, everything else a little-endian one.
	const uint le = data[0x0e] | data[0x0f] << 8, be = data[0x0e] << 8 | data[0x0f];
	return ((le >= 0x20 && le <= 0x100) || (be >= 0x20 && be <= 0x100)) && size >= 0x20;
}

bool IsSEDBRes (const u8 *data, uint size)
{
	return IsSEDB (data, size) && size >= SEDB_RES_HEADER && !memcmp (data + 4, "RES ", 4);
}

bool IsSEDBTexture (const u8 *data, uint size)
{
	return IsSEDB (data, size) && size >= SEDB_PIXELS_OFFSET && !memcmp (data + 4, "txb\0", 4)
		&& !memcmp (data + SEDB_DDS_OFFSET, "DDS ", 4);
}

// Sub-file type tag of the member header ("txb", "shd", "RES "...) as a
// lower case extension; nested RES containers use ".rps" like top-level ones.
static void sedb_type_ext (char *dest, uint dest_size, const u8 *tag)
{
	uint o = 0;
	for (uint i = 0; i < 4 && tag[i] && o + 1 < dest_size; i++)
	{
		const char c = tag[i];
		if (c == ' ')
			continue;
		dest[o++] = c >= 'A' && c <= 'Z' ? c + 'a' - 'A'
			: (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ? c
										: '_';
	}
	dest[o] = 0;
	if (!strcmp (dest, "res"))
		strcpy (dest, "rps");
	if (!*dest)
		strcpy (dest, "bin");
}

enumError ScanSEDBRes (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!IsSEDBRes (data, size))
		return EINVAL;

	const u32 n = sedb_le32 (data + 0x20);
	if (n < 3 || n > SEDB_RES_HEADER + SEDB_MAX_RECORDS
		|| SEDB_RES_HEADER + 16ull * n > size)
		return EINVAL;
	const u64 base = SEDB_RES_HEADER + 16ull * n;
	const u8 *rec = data + SEDB_RES_HEADER;

	// The name table is the last record.
	const u32 name_off = sedb_le32 (rec + 16 * (n - 1) + 4);
	const u32 name_size = sedb_le32 (rec + 16 * (n - 1) + 8);
	if (base + name_off + name_size > size || name_size < 16ull * n)
		return EINVAL;
	const u8 *names = data + base + name_off;

	nintendo_sarc_entry_t *out = CALLOC (n, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint count = 0;
	for (uint i = 0; i < n - 2; i++)
	{
		const u32 off = sedb_le32 (rec + 16 * i + 4);
		const u32 fsize = sedb_le32 (rec + 16 * i + 8);
		if (base + off + fsize > size)
		{
			ResetOwnedEntries (out, count);
			return EINVAL;
		}
		if (fsize < 0x20 || memcmp (data + base + off, "SEDB", 4))
			continue; // string pool / padding record

		char stem[17], ext[8], name[64];
		memcpy (stem, names + 16 * i, 16);
		stem[16] = 0;
		for (char *p = stem; *p; p++)
			if (*p == '/' || *p == '\\' || (u8)*p < 0x20)
				*p = '_';
		if (!*stem)
			snprintf (stem, sizeof (stem), "%04u", i);
		sedb_type_ext (ext, sizeof (ext), data + base + off + 4);
		snprintf (name, sizeof (name), "%s.%s", stem, ext);

		// Names are not unique (e.g. a texture and its shader share one).
		for (uint k = 0; k < count; k++)
			if (!strcmp (out[k].name, name))
			{
				snprintf (name, sizeof (name), "%s_%u.%s", stem, i, ext);
				break;
			}
		if (!OwnedNameOk (name))
			snprintf (name, sizeof (name), "%04u.%s", i, ext);
		if (!OwnedEntryAdd (out, count, name, data + base + off, fsize))
		{
			ResetOwnedEntries (out, count);
			return ERR_CANT_CREATE;
		}
		count++;
	}
	if (!count)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = count;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			texture decoding		///////////////
///////////////////////////////////////////////////////////////////////////////
//

enumError DecodeSEDBTexture (u8 **rgba, uint *width, uint *height, const u8 *data, uint size)
{
	if (!rgba || !IsSEDBTexture (data, size))
		return EINVAL;

	const u8 *dds = data + SEDB_DDS_OFFSET;
	const uint h = sedb_be32 (dds + 0x0c), w = sedb_be32 (dds + 0x10);
	const u32 pf_flags = sedb_be32 (dds + 0x50);
	const u32 fourcc = sedb_le32 (dds + 0x54);
	const u32 bits = sedb_be32 (dds + 0x58);
	if (!w || !h || w > 4096 || h > 4096)
		return EINVAL;
	const u8 *pix = data + SEDB_PIXELS_OFFSET;
	const u64 avail = size - SEDB_PIXELS_OFFSET;

	if (pf_flags == 4 && fourcc == 0x44585431) // "DXT1" -> GX CMPR
	{
		if ((w & 7) || (h & 7) || avail < (u64)w * h / 2)
			return ERR_NOTHING_TO_DO;
		u8 *out = GxDecodeCmprAlpha4 (pix, 0, w, h);
		if (!out)
			return ERR_CANT_CREATE;
		*rgba = out;
	}
	else if ((pf_flags == 0x20001 && bits == 16) || (pf_flags == 0x41 && bits == 32))
	{
		const bool rgba8 = bits == 32;
		const uint bpp = rgba8 ? 4 : 2;
		if (avail < (u64)w * h * bpp)
			return ERR_NOTHING_TO_DO;
		u8 *out = MALLOC ((size_t)w * h * 4);
		if (!out)
			return ERR_CANT_CREATE;
		for (size_t k = 0; k < (size_t)w * h; k++)
		{
			const u8 *p = pix + k * bpp;
			u8 *d = out + k * 4;
			if (rgba8)
				memcpy (d, p, 4);
			else
			{
				d[0] = d[1] = d[2] = p[0];
				d[3] = p[1];
			}
		}
		*rgba = out;
	}
	else if (pf_flags == 0x20 && bits == 8) // 256 entry A,R,G,B palette + indices
	{
		if (avail < 0x400 + (u64)w * h)
			return ERR_NOTHING_TO_DO;
		u8 *out = MALLOC ((size_t)w * h * 4);
		if (!out)
			return ERR_CANT_CREATE;
		for (size_t k = 0; k < (size_t)w * h; k++)
		{
			const u8 *e = pix + 4 * (size_t)pix[0x400 + k];
			u8 *d = out + k * 4;
			d[0] = e[1];
			d[1] = e[2];
			d[2] = e[3];
			d[3] = e[0];
		}
		*rgba = out;
	}
	else
		return ERR_NOTHING_TO_DO;

	*width = w;
	*height = h;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			text summaries			///////////////
///////////////////////////////////////////////////////////////////////////////
//

static float sedb_bef (const u8 *p)
{
	const u32 v = sedb_be32 (p);
	float f;
	memcpy (&f, &v, sizeof (f));
	return f;
}

static void sedb_dump_header (FILE *f, const u8 *data, uint size)
{
	char type[5] = { 0 };
	memcpy (type, data + 4, 4);
	if (!memcmp (type, "PHB", 4))
		fprintf (f, "# SEDB %s, %u bytes (big-endian header)\n", type, size);
	else
		fprintf (f, "# SEDB %s, version %u, %u bytes, build time %u\n", type,
			sedb_le32 (data + 8), size, sedb_le32 (data + 0x18));
}

static void sedb_dump_shader (FILE *f, const u8 *data, uint size)
{
	for (uint pos = 0x40; pos + 0x10 <= size;)
	{
		const u8 *c = data + pos;
		const u32 csize = sedb_be32 (c + 8);
		if (csize < 0x10 || pos + csize > size)
			break;
		fprintf (f, "chunk %.4s, %u bytes\n", (ccp)c, csize);
		if (!memcmp (c, "PRAM", 4) && csize >= 0x30)
		{
			const u8 *b = c + 0x10;
			const u32 n = sedb_be32 (c + 0x18);
			const u32 end = csize - 0x10;
			if (n && n < 256 && 0x0c + 4ull * (5 + 2 * n) <= end)
				for (u32 k = 0; k < n; k++)
				{
					const u32 ro = sedb_be32 (b + 0x0c + 4 * (5 + k));
					const u32 no = sedb_be32 (b + 0x0c + 4 * (5 + n + k));
					if (ro + 0x20 > end || no >= end)
						continue;
					const uint comps = b[ro + 2];
					fprintf (f, "  param %.*s = [", (int)(end - no), (ccp)b + no);
					for (uint i = 0; i < comps && i < 4; i++)
						fprintf (f, "%s%g", i ? ", " : "", sedb_bef (b + ro + 0x10 + 4 * i));
					fprintf (f, "] (flag %u)\n", b[ro]);
				}
		}
		pos += csize;
	}
}

static void sedb_dump_strings (FILE *f, const u8 *data, uint size, uint start)
{
	fprintf (f, "strings:\n");
	for (uint i = start; i < size;)
	{
		uint j = i;
		while (j < size && data[j] >= 0x20 && data[j] < 0x7f)
			j++;
		if (j - i >= 4 && j < size && !data[j])
			fprintf (f, "  %06x %.*s\n", i, (int)(j - i), (ccp)data + i);
		i = j + 1;
	}
}

char *DumpSEDBText (const u8 *data, uint size)
{
	if (!IsSEDB (data, size))
		return 0;
	const bool shd = !memcmp (data + 4, "shd\0", 4);
	const bool lyb = !memcmp (data + 4, "lyb\0", 4);
	const bool phb = !memcmp (data + 4, "PHB\0", 4);
	if (!shd && !lyb && !phb)
		return 0;

	char *text = 0;
	size_t text_size = 0;
	FILE *f = open_memstream (&text, &text_size);
	if (!f)
		return 0;
	sedb_dump_header (f, data, size);
	if (shd)
		sedb_dump_shader (f, data, size);
	else if (lyb)
		sedb_dump_strings (f, data, size, 0x30);
	else
	{
		uint blocks = 0;
		for (uint i = 0x20; i + 8 <= size; i += 0x10)
			if (!memcmp (data + i, "PHB.TBC\0", 8))
				blocks++;
		if (size >= 0x120 && !memcmp (data + 0xc0, "PHB.GBD\0", 8))
		{
			const u8 *b = data + 0x100;
			fprintf (f, "bounds min [%g, %g, %g] max [%g, %g, %g]\n", sedb_bef (b),
				sedb_bef (b + 4), sedb_bef (b + 8), sedb_bef (b + 16), sedb_bef (b + 20),
				sedb_bef (b + 24));
		}
		fprintf (f, "collision blocks: %u\n", blocks);
	}
	fclose (f);
	char *copy = text ? MALLOC (text_size + 1) : 0;
	if (copy)
	{
		memcpy (copy, text, text_size);
		copy[text_size] = 0;
	}
	free (text);
	return copy;
}
