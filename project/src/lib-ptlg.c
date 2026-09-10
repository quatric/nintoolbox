// SPDX-License-Identifier: GPL-2.0+
// Split out of lib-nintendo-archives.c -- one archive format per file.
#include "lib-nintendo-archives.h"
#include "lib-nintendo.h"
#include "lib-image.h"
#include "lib-camelot.h"
#include "lib-yay0.h"
#include "lib-flim.h"
#include "lib-szs.h"
#include "lib-std.h"
#include "lib-zstd.h"
#include "lib-archive-util.h"
#include <zlib.h>
#include <stdlib.h>
#include <string.h>


// ----------------------------------------------------------------------------
// Next Level Games PTLG texture container (.glt / .rlt)
//
// Big-endian throughout. Layout, cross-checked against KillzXGaming's
// StrikersRLT.cs (Switch-Toolbox, MIT) and the retail corpora of Super Mario
// Strikers (GameCube .glt) and Mario Strikers Charged (Wii .rlt):
//
//   0x00  "PTLG"
//   0x04  u32 texture count
//   0x08  u32 unknown  (0 on GameCube)
//   0x0C  u32 padding
//         one u32 of padding follows on some builds: if the next word reads
//         as 0, the entry table starts 16 bytes later, else 4 bytes earlier.
//   ...   texture count * { u32 hash, u32 image offset, u32 section size,
//                           u32 unknown }, offsets relative to table end
//   ...   per texture: u32 mip count, u32 unk, u8 unk, u8 format, u8 unk,
//                      u8 unk, u16 width, u16 height, u16 unk, 3 * u32 unk,
//                      then the raw GX pixel data
//
// The pixel data is plain GX texture data in the same formats TPL wraps, so
// each texture is re-emitted as a standalone TPL rather than a raw blob:
// that makes it directly usable with `wimgt DECODE tex.tpl --dest tex.png`
// instead of needing the dimensions and format carried out of band.
// ----------------------------------------------------------------------------

// PTLG format byte -> image_format_t (StrikersRLT.cs FormatList).
static image_format_t ptlg_image_format (u8 format)
{
	switch (format)
	{
		case 0x2:
			return IMG_I4;
		case 0x3:
			return IMG_I8;
		case 0x4:
			return IMG_IA4;
		case 0x5:
			return IMG_RGB5A3;
		case 0x6:
			return IMG_CMPR;
		case 0x7:
			return IMG_RGB565;
		case 0x8:
			return IMG_RGBA32;
		default:
			return IMG_INVALID;
	}
}


// Decode every texture in a PTLG container to "<hash>.png" in DEST_DIR.
//
// GLG/RLG models bind their textures by the same 32-bit hash PTLG keys its
// entries with, so a model exporter needs the images under exactly those
// names. This shares the container walk and TPL wrapping with
// ExtractPTLGArchive() below; the only difference is the output form.
enumError DecodePTLGToPNGDir (const u8 *data, uint size, ccp dest_dir, uint *n_written)
{
	if (n_written)
		*n_written = 0;
	if (!data || size < 0x20 || !dest_dir || memcmp (data, "PTLG", 4))
		return ERR_NOTHING_TO_DO;

	const u32 n_tex = rd_be32 (data + 4);
	if (!n_tex || n_tex > 0x10000)
		return ERR_NOTHING_TO_DO;
	const bool is_gc = rd_be32 (data + 8) == 0;
	const u32 tab_off = rd_be32 (data + 0x10) == 0 ? 0x20 : 0x10;
	if ((u64)tab_off + (u64)n_tex * 16 > size)
		return ERR_NOTHING_TO_DO;
	const u32 data_base = tab_off + n_tex * 16;

	uint written = 0;
	for (u32 i = 0; i < n_tex; i++)
	{
		const u8 *ent = data + tab_off + i * 16;
		const u32 hash = rd_be32 (ent);
		const u32 img_off = rd_be32 (ent + 4);
		const u32 sect_size = rd_be32 (ent + 8);

		const u64 abs = (u64)data_base + img_off;
		if (abs + 0x20 > size || !sect_size || abs + sect_size > size)
			continue;

		const u8 *th = data + abs;
		const u8 format = th[9];
		const u16 width = is_gc ? rd_be16 (th + 12) : rd_be16 (th + 14);
		const u16 height = is_gc ? rd_be16 (th + 14) : rd_be16 (th + 16);
		const u32 hdr = is_gc ? 16 : 32;

		const image_format_t iform = ptlg_image_format (format);
		if (iform == IMG_INVALID || !width || !height || hdr >= sect_size)
			continue;

		const u32 img_size = sect_size - hdr;
		if (abs + hdr + img_size > size)
			continue;

		const u32 tpl_hdr = sizeof (tpl_header_t);
		const u32 tpl_tab = tpl_hdr + sizeof (tpl_imgtab_t);
		const u32 tpl_data = tpl_tab + sizeof (tpl_img_header_t);
		u8 *tpl = CALLOC (tpl_data + img_size, 1);
		if (!tpl)
			continue;

		write_be32 (tpl, TPL_MAGIC_NUM);
		write_be32 (tpl + 4, 1);
		write_be32 (tpl + 8, tpl_hdr);
		write_be32 (tpl + tpl_hdr, tpl_tab);
		write_be32 (tpl + tpl_hdr + 4, 0);
		write_be16 (tpl + tpl_tab, height);
		write_be16 (tpl + tpl_tab + 2, width);
		write_be32 (tpl + tpl_tab + 4, iform);
		write_be32 (tpl + tpl_tab + 8, tpl_data);
		write_be32 (tpl + tpl_tab + 20, 1);
		write_be32 (tpl + tpl_tab + 24, 1);
		memcpy (tpl + tpl_data, data + abs + hdr, img_size);

		char out[PATH_MAX];
		snprintf (out, sizeof (out), "%s/%08x.png", dest_dir, hash);

		Image_t img;
		if (AssignIMG (&img, 1, tpl, tpl_data + img_size, 0, false, &be_func, out) == ERR_OK
			&& SaveIMG (&img, FF_PNG, 0, 0, out, true) == ERR_OK)
			written++;
		ResetIMG (&img);
		FREE (tpl);
	}

	if (n_written)
		*n_written = written;
	return written ? ERR_OK : ERR_NOTHING_TO_DO;
}


enumError ExtractPTLGArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".glt") && !is_ext_match (arg, ".rlt"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size < 0x20 || memcmp (raw, "PTLG", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 n_tex = rd_be32 (raw + 4);
	if (!n_tex || n_tex > 0x10000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// Word at 0x08 is zero on GameCube (.glt) and a hash on Wii (.rlt);
	// StrikersRLT.cs uses the same discriminator.
	const bool is_gc = rd_be32 (raw + 8) == 0;

	// See the layout note above: some builds carry an extra padding word
	// before the entry table.
	u32 tab_off = rd_be32 (raw + 0x10) == 0 ? 0x20 : 0x10;
	if ((u64)tab_off + (u64)n_tex * 16 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 data_base = tab_off + n_tex * 16;

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	uint written = 0;
	for (u32 i = 0; i < n_tex; i++)
	{
		const u8 *ent = raw + tab_off + i * 16;
		const u32 hash = rd_be32 (ent);
		const u32 img_off = rd_be32 (ent + 4);
		const u32 sect_size = rd_be32 (ent + 8);

		const u64 abs = (u64)data_base + img_off;
		if (abs + 0x20 > raw_size || !sect_size || abs + sect_size > raw_size)
			continue;

		// Per-texture header, common prefix:
		//   0x00 u32 mip count, 0x04 u32 unknown (2), 0x08 u8 unknown (5),
		//   0x09 u8 format, 0x0a u8 unknown (5), 0x0b u8 unknown
		// then width/height, then the GX pixel data including the whole mip
		// chain. GameCube packs width/height at 0x0c/0x0e for a 16-byte
		// header; the Wii build pads two bytes first, putting them at
		// 0x0e/0x10 and padding the header out to 32.
		//
		// Both sizes are confirmed arithmetically rather than assumed: for
		// every CMPR texture in Mario.glt / extratextures.rlt the summed mip
		// chain equals sectionSize minus exactly 16 (GameCube) or 32 (Wii).
		// StrikersRLT.cs instead computes a 30/32-byte header for both,
		// which on GameCube eats 14 bytes of pixel data and leaves the image
		// misaligned (correct dimensions, scrambled colours).
		const u8 *th = raw + abs;
		const u32 mipcount = rd_be32 (th);
		const u8 format = th[9];
		const u16 width = is_gc ? rd_be16 (th + 12) : rd_be16 (th + 14);
		const u16 height = is_gc ? rd_be16 (th + 14) : rd_be16 (th + 16);
		const u32 hdr = is_gc ? 16 : 32;

		const image_format_t iform = ptlg_image_format (format);
		if (iform == IMG_INVALID || !width || !height || hdr >= sect_size)
			continue;

		const u32 img_size = sect_size - hdr;
		if (abs + hdr + img_size > raw_size)
			continue;

		// Wrap the GX pixel data in a minimal single-image TPL.
		const u32 tpl_hdr = sizeof (tpl_header_t); // 0x0c
		const u32 tpl_tab = tpl_hdr + sizeof (tpl_imgtab_t); // 0x14
		const u32 tpl_data = tpl_tab + sizeof (tpl_img_header_t);
		u8 *tpl = CALLOC (tpl_data + img_size, 1);

		write_be32 (tpl, TPL_MAGIC_NUM);
		write_be32 (tpl + 4, 1);
		write_be32 (tpl + 8, tpl_hdr);
		write_be32 (tpl + tpl_hdr, tpl_tab); // image_off
		write_be32 (tpl + tpl_hdr + 4, 0); // palette_off
		write_be16 (tpl + tpl_tab, height);
		write_be16 (tpl + tpl_tab + 2, width);
		write_be32 (tpl + tpl_tab + 4, iform);
		write_be32 (tpl + tpl_tab + 8, tpl_data);
		write_be32 (tpl + tpl_tab + 20, 1); // min_filter
		write_be32 (tpl + tpl_tab + 24, 1); // mag_filter
		memcpy (tpl + tpl_data, raw + abs + hdr, img_size);

		char out[PATH_MAX];
		snprintf (out, sizeof (out), "%s/%08x.tpl", dest, hash);
		if (!testmode)
			SaveFile (out, 0, 0, tpl, tpl_data + img_size, 0);
		FREE (tpl);
		written++;

		if (verbose > 0)
			fprintf (stdlog, "  PTLG texture %08x: %ux%u fmt=%u mips=%u\n", hash, width, height,
				format, mipcount);
	}

	FREE (raw);
	return written ? ERR_OK : ERR_NOTHING_TO_DO;
}


enumError CreatePTLGArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, bool is_gc)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	// Calculate section sizes
	const u32 tab_off = 0x10;
	const u32 table_bytes = n_entries * 16;
	const u32 data_base = tab_off + table_bytes;
	const u32 th_size = is_gc ? 16 : 32;

	// Pass 1: compute offsets and total size
	u32 cur_img_off = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		const nintendo_sarc_entry_t *e = &entries[i];
		u32 payload_sz = 0;
		if (e->data && e->size >= 0x28 && memcmp (e->data, "\x00\x20\xaf\x30", 4) == 0)
		{
			const tpl_header_t *th = (const tpl_header_t *)e->data;
			u32 n_img = be32 (&th->n_image);
			u32 img_tab_off = be32 (&th->imgtab_off);
			if (n_img > 0 && img_tab_off + 4 <= e->size)
			{
				u32 img_hdr_off = be32 (e->data + img_tab_off);
				if (img_hdr_off + sizeof (tpl_img_header_t) <= e->size)
				{
					const tpl_img_header_t *ti = (const tpl_img_header_t *)(e->data + img_hdr_off);
					u32 d_off = be32 (&ti->data_off);
					if (d_off < e->size)
						payload_sz = e->size - d_off;
				}
			}
		}
		if (!payload_sz)
			payload_sz = e->size;

		u32 sect_size = th_size + payload_sz;
		cur_img_off += sect_size;
	}

	const u32 total_size = data_base + cur_img_off;
	u8 *buf = CALLOC (1, total_size);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	// Header
	memcpy (buf, "PTLG", 4);
	write_be32 (buf + 4, n_entries);
	write_be32 (buf + 8, is_gc ? 0 : 0xC31808CF);
	write_be32 (buf + 12, 0);

	// Pass 2: serialize entries and textures
	cur_img_off = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		const nintendo_sarc_entry_t *e = &entries[i];
		u32 hash = 0;
		if (e->name)
		{
			// If filename is hex like "abcd1234.tpl", parse it
			char *endp = 0;
			u32 hval = (u32)strtoul (e->name, &endp, 16);
			if (endp && (*endp == '.' || *endp == 0))
				hash = hval;
			else
				hash = CalcCRC32 (0, (const u8 *)e->name, strlen (e->name));
		}
		if (!hash)
			hash = 0xABCD0000 + i;

		u16 w = 32, h = 32;
		u8 format = 3; // default I8
		const u8 *payload = e->data;
		u32 payload_sz = e->size;

		if (e->data && e->size >= 0x28 && memcmp (e->data, "\x00\x20\xaf\x30", 4) == 0)
		{
			const tpl_header_t *th = (const tpl_header_t *)e->data;
			u32 n_img = be32 (&th->n_image);
			u32 img_tab_off = be32 (&th->imgtab_off);
			if (n_img > 0 && img_tab_off + 4 <= e->size)
			{
				u32 img_hdr_off = be32 (e->data + img_tab_off);
				if (img_hdr_off + sizeof (tpl_img_header_t) <= e->size)
				{
					const tpl_img_header_t *ti = (const tpl_img_header_t *)(e->data + img_hdr_off);
					h = be16 (&ti->height);
					w = be16 (&ti->width);
					u32 iform = be32 (&ti->iform);
					switch (iform)
					{
						case IMG_I4:
							format = 0x2;
							break;
						case IMG_I8:
							format = 0x3;
							break;
						case IMG_IA4:
							format = 0x4;
							break;
						case IMG_RGB5A3:
							format = 0x5;
							break;
						case IMG_CMPR:
							format = 0x6;
							break;
						case IMG_RGB565:
							format = 0x7;
							break;
						case IMG_RGBA32:
							format = 0x8;
							break;
						default:
							format = 0x3;
							break;
					}
					u32 d_off = be32 (&ti->data_off);
					if (d_off < e->size)
					{
						payload = e->data + d_off;
						payload_sz = e->size - d_off;
					}
				}
			}
		}

		u32 sect_size = th_size + payload_sz;

		// Entry in table
		u8 *ent = buf + tab_off + i * 16;
		write_be32 (ent + 0, hash);
		write_be32 (ent + 4, cur_img_off);
		write_be32 (ent + 8, sect_size);
		write_be32 (ent + 12, 0);

		// Per-texture header
		u8 *th = buf + data_base + cur_img_off;
		write_be32 (th + 0, 1); // mip count = 1
		write_be32 (th + 4, 2); // unk
		th[8] = 5;
		th[9] = format;
		th[10] = 5;
		th[11] = 0;
		if (is_gc)
		{
			write_be16 (th + 12, w);
			write_be16 (th + 14, h);
		}
		else
		{
			write_be16 (th + 14, w);
			write_be16 (th + 16, h);
		}

		// Payload
		if (payload && payload_sz > 0)
			memcpy (th + th_size, payload, payload_sz);

		cur_img_off += sect_size;
	}

	*dest = buf;
	*dest_size = total_size;
	return ERR_OK;
}
