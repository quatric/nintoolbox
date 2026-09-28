// SPDX-License-Identifier: GPL-2.0+
// "Destroy All Humans! Big Willy Unleashed" (Wii) formats -- see
// lib-dahbwu.h for exactly what is and is not understood about each of
// them.

#include "lib-dahbwu.h"
#include "lib-nintendo.h"
#include <string.h>
#include <ctype.h>
#include <zlib.h>

//-----------------------------------------------------------------------------
// shared helpers

static void print_escaped (FILE *f, const u8 *data, size_t len)
{
	for (size_t i = 0; i < len; i++)
	{
		u8 c = data[i];
		if (c == '\\')
			fputs ("\\\\", f);
		else if (c >= 0x20 && c < 0x7f)
			fputc (c, f);
		else
			fprintf (f, "\\x%02x", c);
	}
}

// UTF-16BE -> escaped-ASCII, best-effort (BMP-only, non-ASCII code points
// are emitted as \uXXXX).
static void print_escaped_utf16be (FILE *f, const u8 *data, size_t units)
{
	for (size_t i = 0; i < units; i++)
	{
		u16 c = rd_be16 (data + i * 2);
		if (c == '\\')
			fputs ("\\\\", f);
		else if (c >= 0x20 && c < 0x7f)
			fputc ((char)c, f);
		else
			fprintf (f, "\\u%04x", c);
	}
}

//-----------------------------------------------------------------------------
// (1) ".stream" asset/level/UI resource stream

#define DAHBWU_STREAM_HDR_SIZE 44 // 4 (tag) + 4 (four) + 32 (hash) + 4 (comp_size)
#define DAHBWU_STREAM_MAX_OUT (256 * 1024 * 1024) // sanity cap on inflated size

int IsDahbwuStream (const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!data || size < DAHBWU_STREAM_HDR_SIZE)
		return 0;

	// bytes 4..8 are confirmed == 4 in every real sample on the disc.
	if (rd_be32 (data + 4) != 4)
		return 0;

	// bytes 8..40 are confirmed ASCII lowercase hex digits (MD5-shaped) in
	// every real sample.
	for (int i = 0; i < 32; i++)
	{
		u8 c = data[8 + i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return 0;
	}

	u32 comp_size = rd_be32 (data + 40);
	if ((u64)DAHBWU_STREAM_HDR_SIZE + comp_size > size)
		return 0;
	if (comp_size < 2)
		return 0;

	// zlib stream, confirmed to start "78 DA" (best compression) in every
	// real sample seen; accept any valid zlib header byte pair to be
	// slightly more permissive than the single observed variant.
	const u8 *z = data + DAHBWU_STREAM_HDR_SIZE;
	if ((z[0] & 0x0f) != 8 /* CM = deflate */)
		return 0;
	if (((z[0] << 8) | z[1]) % 31)
		return 0;

	return 1;
}

enumError DecompressDahbwuStream (const u8 *data, size_t size, u8 **out_data, size_t *out_size)
{
	if (!data || !out_data || !out_size)
		return EINVAL;
	*out_data = 0;
	*out_size = 0;
	if (!IsDahbwuStream (data, size, size))
		return ERR_INVALID_DATA;

	u32 comp_size = rd_be32 (data + 40);
	const u8 *src = data + DAHBWU_STREAM_HDR_SIZE;

	z_stream strm;
	memset (&strm, 0, sizeof (strm));
	if (inflateInit (&strm) != Z_OK)
		return ERR_INVALID_DATA;

	size_t cap = comp_size * 4 + 256; // initial guess, grown below as needed
	u8 *out = MALLOC (cap);
	strm.next_in = (Bytef *)src;
	strm.avail_in = comp_size;
	strm.next_out = out;
	strm.avail_out = cap;

	int ret;
	for (;;)
	{
		ret = inflate (&strm, Z_NO_FLUSH);
		if (ret == Z_STREAM_END)
			break;
		if (ret != Z_OK && ret != Z_BUF_ERROR)
		{
			inflateEnd (&strm);
			FREE (out);
			return ERR_INVALID_DATA;
		}
		if (strm.avail_out == 0)
		{
			if (cap >= DAHBWU_STREAM_MAX_OUT)
			{
				inflateEnd (&strm);
				FREE (out);
				return ERR_INVALID_DATA;
			}
			size_t used = cap;
			cap *= 2;
			out = REALLOC (out, cap);
			strm.next_out = out + used;
			strm.avail_out = cap - used;
		}
		else if (ret == Z_BUF_ERROR)
		{
			// no forward progress possible and no room left / no more input
			inflateEnd (&strm);
			FREE (out);
			return ERR_INVALID_DATA;
		}
	}

	size_t produced = cap - strm.avail_out;
	inflateEnd (&strm);

	*out_data = out;
	*out_size = produced;
	return ERR_OK;
}

enumError DecodeDahbwuStream_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsDahbwuStream (data, size, file_size))
		return EINVAL;

	u32 tag = rd_be32 (data);
	u32 comp_size = rd_be32 (data + 40);

	fprintf (f, "# Destroy All Humans! Big Willy Unleashed asset stream (.stream)\n");
	fprintf (f, "tag = 0x%x  # per-asset id/hash, not a fixed magic\n", tag);
	fprintf (f, "four = 4  # confirmed constant across every real sample\n");
	fprintf (f, "content_hash = \"");
	print_escaped (f, data + 8, 32);
	fprintf (f, "\"  # ASCII hex, MD5-shaped\n");
	fprintf (f, "comp_size = %u\n", comp_size);

	u8 *dec = 0;
	size_t dec_size = 0;
	enumError err = DecompressDahbwuStream (data, size, &dec, &dec_size);
	if (err != ERR_OK)
	{
		fprintf (f, "# zlib payload failed to inflate\n");
		return ERR_OK;
	}

	fprintf (f, "decompressed_size = %zu\n", dec_size);
	fprintf (f, "# decompressed payload is the engine's own generic \"Chunk\" object\n");
	fprintf (f, "# stream (see Chunk.cpp/ChunkManager.cpp in dah_rls.elf) -- the per-tag\n");
	fprintf (f, "# chunk table is NOT reverse-engineered; see lib-dahbwu.h note (1).\n");
	fprintf (f, "# Leading bytes of the decompressed payload, escaped:\n");
	fprintf (f, "payload_preview = \"");
	print_escaped (f, dec, dec_size < 256 ? dec_size : 256);
	fprintf (f, "\"\n");

	FREE (dec);
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".tbl" localized UTF-16BE string table

int IsDahbwuTbl (const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!data || size < 4)
		return 0;

	u32 count = rd_be32 (data);
	if (!count)
		return size == 4; // confirmed valid empty-table state (6 real samples)

	size_t off = 4;
	for (u32 i = 0; i < count; i++)
	{
		if (off + 4 > size)
			return 0;
		u32 key_len = rd_be32 (data + off);
		off += 4;
		if (off + key_len > size)
			return 0;
		for (u32 k = 0; k < key_len; k++)
		{
			u8 c = data[off + k];
			if (c < 0x20 || c >= 0x7f)
				return 0;
		}
		off += key_len;

		if (off + 4 > size)
			return 0;
		u32 str_units = rd_be32 (data + off);
		off += 4;
		u64 str_bytes = (u64)str_units * 2;
		if (off + str_bytes > size)
			return 0;
		off += str_bytes;
	}
	return off == size;
}

enumError DecodeDahbwuTbl_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsDahbwuTbl (data, size, file_size))
		return EINVAL;

	fprintf (f, "# Destroy All Humans! Big Willy Unleashed localized string table (.tbl)\n");
	u32 count = rd_be32 (data);
	fprintf (f, "count = %u\n", count);

	size_t off = 4;
	for (u32 i = 0; i < count; i++)
	{
		u32 key_len = rd_be32 (data + off);
		off += 4;
		const u8 *key = data + off;
		off += key_len;

		u32 str_units = rd_be32 (data + off);
		off += 4;
		const u8 *str = data + off;
		off += (u64)str_units * 2;

		fprintf (f, "\"");
		print_escaped (f, key, key_len);
		fprintf (f, "\" = \"");
		print_escaped_utf16be (f, str, str_units);
		fprintf (f, "\"\n");
	}
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (3) ".cnv" conversation/speaker/line-ID table

// Structural-only walk (no output), used by IsDahbwuCnv() and by the text
// decoder to re-derive offsets. Returns true iff the file is fully and
// exactly consumed as conv_count top-level records.
static int dahbwu_cnv_read_str (const u8 *data, size_t size, size_t *off, size_t *out_off, u32 *out_len)
{
	if (*off + 4 > size)
		return 0;
	u32 len = rd_be32 (data + *off);
	*off += 4;
	if (*off + len > size)
		return 0;
	for (u32 k = 0; k < len; k++)
	{
		u8 c = data[*off + k];
		if (c < 0x20 || c >= 0x7f)
			return 0;
	}
	*out_off = *off;
	*out_len = len;
	*off += len;
	return 1;
}

int IsDahbwuCnv (const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!data || size < 4)
		return 0;

	size_t off = 0;
	u32 conv_count = rd_be32 (data + off);
	off += 4;

	for (u32 c = 0; c < conv_count; c++)
	{
		size_t name_off;
		u32 name_len;
		if (!dahbwu_cnv_read_str (data, size, &off, &name_off, &name_len))
			return 0;

		if (off + 4 > size)
			return 0;
		u32 line_count = rd_be32 (data + off);
		off += 4;

		for (u32 l = 0; l < line_count; l++)
		{
			size_t speaker_off, id_off;
			u32 speaker_len, id_len;
			if (!dahbwu_cnv_read_str (data, size, &off, &speaker_off, &speaker_len))
				return 0;
			if (!dahbwu_cnv_read_str (data, size, &off, &id_off, &id_len))
				return 0;
		}
	}
	return off == size;
}

enumError DecodeDahbwuCnv_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data)
		return EINVAL;
	if (!IsDahbwuCnv (data, size, file_size))
		return EINVAL;

	fprintf (f, "# Destroy All Humans! Big Willy Unleashed conversation table (.cnv)\n");

	size_t off = 0;
	u32 conv_count = rd_be32 (data + off);
	off += 4;
	fprintf (f, "conv_count = %u\n", conv_count);

	for (u32 c = 0; c < conv_count; c++)
	{
		size_t name_off;
		u32 name_len;
		dahbwu_cnv_read_str (data, size, &off, &name_off, &name_len);

		u32 line_count = rd_be32 (data + off);
		off += 4;

		fprintf (f, "conversation \"");
		print_escaped (f, data + name_off, name_len);
		fprintf (f, "\" {  # %u line(s)\n", line_count);

		for (u32 l = 0; l < line_count; l++)
		{
			size_t speaker_off, id_off;
			u32 speaker_len, id_len;
			dahbwu_cnv_read_str (data, size, &off, &speaker_off, &speaker_len);
			dahbwu_cnv_read_str (data, size, &off, &id_off, &id_len);

			fprintf (f, "  \"");
			print_escaped (f, data + speaker_off, speaker_len);
			fprintf (f, "\" -> \"");
			print_escaped (f, data + id_off, id_len);
			fprintf (f, "\"\n");
		}
		fprintf (f, "}\n");
	}
	return ERR_OK;
}
