// SPDX-License-Identifier: GPL-2.0+
// "Zack & Wiki: Quest for Barbaros' Treasure" (Wii) formats -- see
// lib-zackwiki.h for exactly what is and is not understood about each.

#include "lib-zackwiki.h"
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
		if (!c)
			break;
		if (c == '\\')
			fputs ("\\\\", f);
		else if (c >= 0x20 && c < 0x7f)
			fputc (c, f);
		else
			fprintf (f, "\\x%02x", c);
	}
}

static void print_tag4 (FILE *f, const u8 *tag)
{
	for (int i = 0; i < 4; i++)
	{
		u8 c = tag[i];
		if (c >= 0x20 && c < 0x7f)
			fputc (c, f);
		else
			fprintf (f, "\\x%02x", c);
	}
}

//-----------------------------------------------------------------------------
// (1) ".tm2" PS2 TIM2 texture

int IsZackWikiTm2 (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 16 || memcmp (data, "TIM2", 4))
		return 0;
	u8 version = data[4];
	u8 format_id = data[5];
	if (version != 4 || (format_id != 0 && format_id != 1))
		return 0;
	u16 image_count = rd_le16 (data + 6);
	if (!image_count || image_count > 256)
		return 0;

	size_t hdr_off = 16;
	if (format_id == 1)
		hdr_off = (hdr_off + 0x7f) & ~(size_t)0x7f;

	if (hdr_off + 48 > size)
		return size < file_size; // short probe buffer: plausible header only

	u32 total_size = rd_le32 (data + hdr_off);
	u32 clut_size = rd_le32 (data + hdr_off + 4);
	u32 image_size = rd_le32 (data + hdr_off + 8);
	u16 header_size = rd_le16 (data + hdr_off + 12);
	if (header_size != 0x30 && header_size != 0x80)
		return 0;
	if ((u64)header_size + image_size + clut_size != total_size)
		return 0;
	if (!image_size)
		return 0;

	return hdr_off + total_size <= file_size;
}

enumError DecodeZackWikiTm2_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!IsZackWikiTm2 (data, size, file_size))
		return ERR_INVALID_DATA;

	u8 format_id = data[5];
	u16 image_count = rd_le16 (data + 6);
	size_t hdr_off = 16;
	if (format_id == 1)
		hdr_off = (hdr_off + 0x7f) & ~(size_t)0x7f;

	fprintf (f, "# Zack & Wiki TIM2 texture (.tm2)\n");
	fprintf (f, "version = 4\n");
	fprintf (f, "format_id = %u\n", format_id);
	fprintf (f, "image_count = %u\n", image_count);

	if (hdr_off + 48 <= size)
	{
		const u8 *h = data + hdr_off;
		u32 total_size = rd_le32 (h);
		u32 clut_size = rd_le32 (h + 4);
		u32 image_size = rd_le32 (h + 8);
		u16 header_size = rd_le16 (h + 12);
		u16 clut_colors = rd_le16 (h + 14);
		u8 pict_format = h[16];
		u8 mipmap_count = h[17];
		u8 clut_type = h[18];
		u8 image_type = h[19];
		u16 image_width = rd_le16 (h + 20);
		u16 image_height = rd_le16 (h + 22);

		fprintf (f, "\n[image0]\n");
		fprintf (f, "header_offset = 0x%zx\n", hdr_off);
		fprintf (f, "header_size = 0x%x\n", header_size);
		fprintf (f, "total_size = %u\n", total_size);
		fprintf (f, "image_size = %u\n", image_size);
		fprintf (f, "clut_size = %u\n", clut_size);
		fprintf (f, "clut_colors = %u\n", clut_colors);
		fprintf (f, "pict_format = 0x%02x\n", pict_format);
		fprintf (f, "mipmap_count = %u\n", mipmap_count);
		fprintf (f, "clut_type = 0x%02x\n", clut_type);
		fprintf (f, "image_type = 0x%02x\n", image_type);
		fprintf (f, "width = %u\n", image_width);
		fprintf (f, "height = %u\n", image_height);
		fprintf (f, "# GS registers/pixel data not decoded -- see lib-zackwiki.h\n");
	}
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".ppg" pCMP zlib-compressed container

// Inflate a bare zlib stream; returns bytes actually consumed from src, or 0
// on failure. *out_size receives the inflated length.
static size_t ppg_inflate_one (const u8 *src, size_t src_size, size_t *out_size)
{
	*out_size = 0;
	if (!src || src_size < 6)
		return 0;

	z_stream strm;
	memset (&strm, 0, sizeof (strm));
	if (inflateInit (&strm) != Z_OK)
		return 0;

	strm.next_in = (Bytef *)src;
	strm.avail_in = (uInt)src_size;

	size_t cap = src_size * 4 + 4096;
	if (cap > 0x08000000u)
		cap = 0x08000000u;
	u8 *out = MALLOC (cap);
	int ret = Z_OK;
	while (out)
	{
		strm.next_out = out + strm.total_out;
		strm.avail_out = (uInt)(cap - strm.total_out);
		ret = inflate (&strm, Z_NO_FLUSH);
		if (ret == Z_STREAM_END || strm.avail_out)
			break;
		if (cap >= 0x08000000u)
			break;
		const size_t ncap = cap > 0x04000000u ? 0x08000000u : cap * 2;
		u8 *n = REALLOC (out, ncap);
		if (!n)
			break;
		out = n;
		cap = ncap;
	}

	size_t consumed = ret == Z_STREAM_END ? src_size - strm.avail_in : 0;
	*out_size = ret == Z_STREAM_END ? strm.total_out : 0;
	inflateEnd (&strm);
	FREE (out);
	return consumed;
}

int IsZackWikiPpg (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 20 || memcmp (data, "pCMP", 4))
		return 0;
	if (size < 18 || data[16] != 0x78) // zlib CMF byte, standard deflate window
		return size < file_size;

	size_t out_size = 0;
	size_t consumed = ppg_inflate_one (data + 16, size - 16, &out_size);
	if (!consumed)
		return size < file_size; // probe buffer may be too short to finish
	u32 decomp_size = rd_be32 (data + 12);
	return decomp_size == 0 || out_size == decomp_size;
}

enumError DecodeZackWikiPpg_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!IsZackWikiPpg (data, size, file_size))
		return ERR_INVALID_DATA;

	fprintf (f, "# Zack & Wiki pCMP compressed container (.ppg)\n");
	fprintf (f, "# format confirmed: 16-byte BE header + raw zlib stream per block,\n");
	fprintf (f, "# see lib-zackwiki.h for what is/isn't confirmed about block framing\n\n");

	size_t pos = 0;
	int block_no = 0;
	while (pos + 16 <= size)
	{
		if (memcmp (data + pos, "pCMP", 4))
			break;
		u32 block_size = rd_be32 (data + pos + 4);
		u32 unknown = rd_be32 (data + pos + 8);
		u32 decomp_size = rd_be32 (data + pos + 12);

		fprintf (f, "[block%d]\n", block_no);
		fprintf (f, "file_offset = 0x%zx\n", pos);
		fprintf (f, "block_size = %u\n", block_size);
		fprintf (f, "unknown = %u\n", unknown);
		fprintf (f, "decomp_size = %u\n", decomp_size);

		size_t out_size = 0;
		size_t consumed = ppg_inflate_one (data + pos + 16, size - pos - 16, &out_size);
		if (!consumed)
		{
			fprintf (f, "inflate = FAILED\n\n");
			break;
		}
		fprintf (f, "inflate = ok (%zu -> %zu bytes)\n\n", consumed, out_size);

		// Resync on the next block: prefer the header's own block_size,
		// but fall back to scanning a small window for "pCMP" since a
		// residual 1-2 byte gap was observed in practice (see header
		// comment).
		size_t next = pos + block_size;
		if (next + 4 <= size && !memcmp (data + next, "pCMP", 4))
		{
			pos = next;
		}
		else
		{
			size_t scan = pos + 16 + consumed;
			size_t limit = scan + 8 < size ? scan + 8 : size - 4 >= scan ? size - 4 : scan;
			size_t found = 0;
			for (size_t p = scan; p + 4 <= size && p <= limit; p++)
			{
				if (!memcmp (data + p, "pCMP", 4))
				{
					found = p;
					break;
				}
			}
			if (!found)
				break;
			pos = found;
		}
		block_no++;
	}

	fprintf (f, "total_blocks = %d\n", block_no);
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (3) shared ".tsb"/".whd" chunk-directory walker

#define ZW_MAX_CHUNKS 64

// Both formats share the exact same on-disc shape: a run of BE u32 offsets
// (the "directory"), then tagged chunks. Returns the number of directory
// entries found (0 on failure). offs[] receives the raw offsets.
static int zw_probe_directory (const u8 *data, size_t size, u32 *offs, int max)
{
	if (!data || size < 8)
		return 0;
	int n = 0;
	u32 first = rd_be32 (data);
	if (first < 4 || (first & 3) || first > size)
		return 0;
	int dir_count = first / 4;
	if (dir_count < 1 || dir_count > max)
		return 0;
	if ((size_t)dir_count * 4 > size)
		return 0;

	u32 prev = 0;
	for (int i = 0; i < dir_count; i++)
	{
		u32 off = rd_be32 (data + i * 4);
		if (off < prev || off > size)
			return 0;
		if (off && off < (u32)(dir_count * 4))
			return 0;
		offs[n++] = off;
		prev = off;
	}
	return n;
}

static int zw_is_chunked_bank (const u8 *data, size_t size, size_t file_size)
{
	u32 offs[ZW_MAX_CHUNKS];
	int n = zw_probe_directory (data, size, offs, ZW_MAX_CHUNKS);
	if (n < 1)
		return size < file_size;

	int checked = 0;
	for (int i = 0; i < n; i++)
	{
		if (!offs[i])
			continue;
		if (offs[i] + 16 > size)
			return size < file_size;
		const u8 *tag = data + offs[i];
		for (int j = 0; j < 4; j++)
			if (!(isalnum (tag[j]) || tag[j] == 0))
				return 0;
		checked++;
	}
	return checked > 0;
}

static void zw_decode_chunked_bank (FILE *f, const u8 *data, size_t size, const char *label)
{
	fprintf (f, "# Zack & Wiki %s\n", label);
	fprintf (f, "# format confirmed: BE u32 chunk-offset directory + tagged\n");
	fprintf (f, "# chunks (tag + count + 2 reserved fields); per-record field\n");
	fprintf (f, "# layout beyond that is NOT fully confirmed, see lib-zackwiki.h\n\n");

	u32 offs[ZW_MAX_CHUNKS];
	int n = zw_probe_directory (data, size, offs, ZW_MAX_CHUNKS);
	fprintf (f, "directory_entries = %d\n\n", n);

	for (int i = 0; i < n; i++)
	{
		if (!offs[i] || offs[i] + 16 > size)
			continue;
		const u8 *c = data + offs[i];
		fprintf (f, "[chunk%d]\n", i);
		fprintf (f, "offset = 0x%x\n", offs[i]);
		fprintf (f, "tag = \"");
		print_tag4 (f, c);
		fprintf (f, "\"\n");
		u32 count = rd_be32 (c + 4);
		u32 res0 = rd_be32 (c + 8);
		u32 res1 = rd_be32 (c + 12);
		fprintf (f, "count = %u\n", count);
		fprintf (f, "reserved0 = %u\n", res0);
		fprintf (f, "reserved1 = %u\n", res1);

		// Dump a bounded amount of raw record bytes following the
		// 16-byte chunk header, since per-field layout is not fully
		// confirmed (see lib-zackwiki.h).
		size_t body = offs[i] + 16;
		size_t next_off = size;
		for (int j = i + 1; j < n; j++)
			if (offs[j] > offs[i])
			{
				next_off = offs[j];
				break;
			}
		size_t dump_len = next_off > body ? next_off - body : 0;
		if (dump_len > 256)
			dump_len = 256;
		if (dump_len)
		{
			fprintf (f, "raw = ");
			for (size_t k = 0; k < dump_len && body + k < size; k++)
				fprintf (f, "%02x", data[body + k]);
			fprintf (f, "\n");
		}
		fprintf (f, "\n");
	}
}

//-----------------------------------------------------------------------------
// (3a) ".tsb"

int IsZackWikiTsb (const u8 *data, size_t size, size_t file_size)
{
	return zw_is_chunked_bank (data, size, file_size);
}

enumError DecodeZackWikiTsb_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!IsZackWikiTsb (data, size, file_size))
		return ERR_INVALID_DATA;
	zw_decode_chunked_bank (f, data, size, "sound bank (.tsb)");
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (3b) ".whd"

int IsZackWikiWhd (const u8 *data, size_t size, size_t file_size)
{
	return zw_is_chunked_bank (data, size, file_size);
}

enumError DecodeZackWikiWhd_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!IsZackWikiWhd (data, size, file_size))
		return ERR_INVALID_DATA;
	zw_decode_chunked_bank (f, data, size, "sound bank (.whd)");
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (4) ".mds" MDSV resource/level container

int IsZackWikiMds (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 4 || memcmp (data, "MDSV", 4))
		return 0;
	// LOAD/MDFD tags confirmed present near the start of every sample,
	// but at a position that varies slightly -- scan a bounded window
	// rather than asserting a fixed offset (see lib-zackwiki.h).
	size_t scan = size < 256 ? size : 256;
	if (scan < 32)
		return size < file_size;
	size_t load_at = 0;
	for (size_t p = 4; p + 4 <= scan; p++)
		if (!memcmp (data + p, "LOAD", 4))
		{
			load_at = p;
			break;
		}
	if (!load_at)
		return size < file_size;
	if (load_at + 8 <= scan && memcmp (data + load_at + 4, "MDFD", 4))
		return 0;
	return 1;
}

enumError DecodeZackWikiMds_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!IsZackWikiMds (data, size, file_size))
		return ERR_INVALID_DATA;

	fprintf (f, "# Zack & Wiki MDSV resource/level container (.mds)\n");
	fprintf (f, "# structural probe only (magic + LOAD/MDFD tag presence) --\n");
	fprintf (f, "# chunk framing and payload are NOT reverse-engineered,\n");
	fprintf (f, "# see lib-zackwiki.h\n\n");
	fprintf (f, "magic = \"MDSV\"\n");
	fprintf (f, "file_size = %zu\n", file_size);

	size_t scan = size < 256 ? size : 256;
	for (size_t p = 4; p + 4 <= scan; p++)
		if (!memcmp (data + p, "LOAD", 4))
		{
			fprintf (f, "LOAD_offset = 0x%zx\n", p);
			if (p + 8 <= scan && !memcmp (data + p + 4, "MDFD", 4))
				fprintf (f, "MDFD_offset = 0x%zx\n", p + 4);
			break;
		}
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (5) ".ssd" streamed ADPCM audio (extension-only, unconfirmed structure)

int IsZackWikiSsd (const u8 *data, size_t size, size_t file_size)
{
	// No fixed magic or offset/size table was found in any real sample
	// pulled from this disc -- see lib-zackwiki.h note (5). This probe is
	// intentionally NOT wired into the magic-based auto-detector in
	// lib-file.c (it would false-positive on arbitrary binary data); it
	// exists only so the decoder can be reached via explicit extension
	// match, same as The Dog Island's ".sci"/".qci".
	(void)data;
	return size > 0 && size == file_size;
}

enumError DecodeZackWikiSsd_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void)data;
	fprintf (f, "# Zack & Wiki streamed ADPCM audio (.ssd)\n");
	fprintf (f, "# NOT reverse-engineered: no magic, tag or offset/size table\n");
	fprintf (f, "# was found in any real sample -- this file appears to be raw\n");
	fprintf (f, "# ADPCM sample data with no embedded index/header at all. See\n");
	fprintf (f, "# lib-zackwiki.h note (5) for what was checked.\n\n");
	fprintf (f, "file_size = %zu\n", file_size);
	fprintf (f, "structure = unconfirmed (extension-recognized only)\n");
	return ERR_OK;
}
