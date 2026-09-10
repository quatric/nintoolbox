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
// Camelot GX texture bank (Mario Golf: Toadstool Tour, Mario Power Tennis)
//
// Camelot's GameCube discs name every asset with single letters (files/A/U/R),
// so there is no extension to key on and the container is usually wrapped in
// Camelot's own LZ codec. Layout, big-endian, after decompression:
//
//   0x00  u32 magic 0x0020af30
//   0x04  u32 texture count
//   0x08  u32 offset of the entry-pointer table
//         table: count * { u32 entry offset, u32 pad }
//   entry:
//     0x00  u16 width, u16 height
//     0x04  u32 GX texture format (the hardware's own numbering)
//     0x08  u32 offset of the pixel data from the start of the file
//     0x0c  sampler state
//
// Verified across every such file on both retail discs: 881 containers,
// 4252 texture entries, of which 4246 have a format/geometry whose GX size
// lands inside the file (670 of 676 single-entry files end exactly at the
// computed size). Palettised formats are declined rather than guessed at --
// three files disc-wide use one and where their palette lives is unknown.
// ----------------------------------------------------------------------------

#define CAMELOT_TEXBANK_MAGIC 0x0020af30

static image_format_t camelot_gx_image_format (u32 format)
{
	switch (format)
	{
		case 0:
			return IMG_I4;
		case 1:
			return IMG_I8;
		case 2:
			return IMG_IA4;
		case 3:
			return IMG_IA8;
		case 4:
			return IMG_RGB565;
		case 5:
			return IMG_RGB5A3;
		case 6:
			return IMG_RGBA32;
		case 14:
			return IMG_CMPR;
		default:
			return IMG_INVALID; // includes C4/C8/C14X2: palette location unknown
	}
}


// Bytes one GX level occupies, tile padding included.
static u32 camelot_gx_level_size (u32 format, u32 w, u32 h)
{
	uint bw, bh, bpp;
	switch (format)
	{
		case 0:
		case 14:
			bw = 8, bh = 8, bpp = 4;
			break;
		case 1:
		case 2:
			bw = 8, bh = 4, bpp = 8;
			break;
		case 3:
		case 4:
		case 5:
			bw = 4, bh = 4, bpp = 16;
			break;
		case 6:
			bw = 4, bh = 4, bpp = 32;
			break;
		default:
			return 0;
	}
	return ((w + bw - 1) / bw) * ((h + bh - 1) / bh) * bw * bh * bpp / 8;
}


// True when a bank header at RAW describes entries that all land inside the
// AVAIL bytes that follow it. Every offset in a bank is relative to the bank
// itself, not to the file, which is what lets one sit inside a larger module.
//
// Camelot's models are PPC relocatable modules (elfbin/xcmdl_*.sbn on Wii)
// that carry their textures inline, so the magic routinely appears at an
// offset rather than at the start of a file. Scanning for it needs this
// check: four bytes alone would false-positive on ordinary code and data.
static bool camelot_bank_valid (const u8 *raw, uint avail)
{
	if (avail < 16 || rd_be32 (raw) != CAMELOT_TEXBANK_MAGIC)
		return false;
	const u32 count = rd_be32 (raw + 4);
	const u32 tbl = rd_be32 (raw + 8);
	if (!count || count > 0x1000 || (u64)tbl + (u64)count * 8 > avail)
		return false;

	for (u32 i = 0; i < count; i++)
	{
		const u32 eoff = rd_be32 (raw + tbl + i * 8);
		if ((u64)eoff + 16 > avail)
			return false;
		const u32 w = rd_be16 (raw + eoff);
		const u32 h = rd_be16 (raw + eoff + 2);
		const u32 format = rd_be32 (raw + eoff + 4);
		const u32 doff = rd_be32 (raw + eoff + 8);
		const u32 need = camelot_gx_level_size (format, w, h);
		if (!w || !h || !need || (u64)doff + need > avail)
			return false;
	}
	return true;
}


// Write every texture in one bank to "<stem>_%04u.png" in DEST, numbering
// from *NEXT so a module holding several banks stays consistently numbered.
static uint camelot_texbank_to_pngs (const u8 *raw, uint size, ccp dest, ccp stem, uint *next)
{
	if (!camelot_bank_valid (raw, size))
		return 0;
	const u32 count = rd_be32 (raw + 4);
	const u32 tbl = rd_be32 (raw + 8);

	uint written = 0;
	for (u32 i = 0; i < count; i++)
	{
		const u32 eoff = rd_be32 (raw + tbl + i * 8);
		if ((u64)eoff + 16 > size)
			continue;
		const u32 w = rd_be16 (raw + eoff);
		const u32 h = rd_be16 (raw + eoff + 2);
		const u32 format = rd_be32 (raw + eoff + 4);
		const u32 doff = rd_be32 (raw + eoff + 8);

		const image_format_t iform = camelot_gx_image_format (format);
		const u32 need = camelot_gx_level_size (format, w, h);
		if (iform == IMG_INVALID || !w || !h || !need || (u64)doff + need > size)
			continue;

		// Wrap the GX pixels in a one-image TPL, the same shape the PTLG
		// reader builds, then let the image layer turn it into a PNG.
		const u32 tpl_hdr = sizeof (tpl_header_t);
		const u32 tpl_tab = tpl_hdr + sizeof (tpl_imgtab_t);
		const u32 tpl_data = tpl_tab + sizeof (tpl_img_header_t);
		u8 *tpl = CALLOC (tpl_data + need, 1);
		if (!tpl)
			continue;

		write_be32 (tpl, TPL_MAGIC_NUM);
		write_be32 (tpl + 4, 1);
		write_be32 (tpl + 8, tpl_hdr);
		write_be32 (tpl + tpl_hdr, tpl_tab);
		write_be32 (tpl + tpl_hdr + 4, 0);
		write_be16 (tpl + tpl_tab, h);
		write_be16 (tpl + tpl_tab + 2, w);
		write_be32 (tpl + tpl_tab + 4, iform);
		write_be32 (tpl + tpl_tab + 8, tpl_data);
		write_be32 (tpl + tpl_tab + 20, 1);
		write_be32 (tpl + tpl_tab + 24, 1);
		memcpy (tpl + tpl_data, raw + doff, need);

		char out[PATH_MAX];
		snprintf (out, sizeof (out), "%s/%s_%04u.png", dest, stem, (*next)++);

		Image_t img;
		if (AssignIMG (&img, 1, tpl, tpl_data + need, 0, false, &be_func, out) == ERR_OK
			&& SaveIMG (&img, FF_PNG, 0, 0, out, true) == ERR_OK)
			written++;
		ResetIMG (&img);
		FREE (tpl);
	}
	return written;
}


// Detection is deliberately structural rather than name- or first-byte-based:
// a Camelot LZ stream only announces itself with a 1 or 2 in byte 0, far too
// weak on its own (which is why GetNintendoFormat() only accepts it for an
// explicit .stpl/.camelot name). Requiring the decompressed result to carry
// the bank magic is the strong signal, and costs one bounded decode.
enumError ExtractCamelotTexBank (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	// A bank is found by scanning for its signature, because Camelot stores
	// banks inside relocatable modules that announce nothing themselves. That
	// scan has no business running over a file that does announce itself: a
	// 432 KB U8 archive from Excite Truck matched the signature 59 times and
	// was extracted as "59 textures in 59 banks", displacing the archive's
	// real contents. Anything carrying a known magic is left to the handler
	// for that format.
	// [[analyse-magic]]
	if (raw_size >= 8 && GetByMagicFF (raw, (uint)raw_size, (uint)raw_size) != FF_UNKNOWN)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	if (raw_size < 16 || raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Decompress first when this is a Camelot stream, then look for banks in
	// whatever we ended up with.
	const u8 *bank = raw;
	uint bank_size = (uint)raw_size;
	u8 *decoded = 0;
	if (raw[0] == 1 || raw[0] == 2)
	{
		uint decoded_size = 0;
		if (!DecodeCamelot (&decoded, &decoded_size, raw, (uint)raw_size) && decoded
			&& decoded_size >= 16)
		{
			bank = decoded;
			bank_size = decoded_size;
		}
	}

	// Count the banks first so nothing is written for a file that turns out
	// to hold none. A bank can sit at any 4-byte-aligned offset: Camelot's
	// models are PPC modules with their textures inline.
	uint n_banks = 0, n_textures = 0;
	for (uint off = 0; off + 16 <= bank_size; off += 4)
		if (camelot_bank_valid (bank + off, bank_size - off))
		{
			n_banks++;
			n_textures += rd_be32 (bank + off + 4);
		}
	if (!n_banks)
	{
		FREE (decoded);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	ccp stem = strrchr (arg, '/');
	stem = stem ? stem + 1 : arg;

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CAMELOT-TEX:%s (%u textures in %u bank%s) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n_textures, n_banks,
			n_banks == 1 ? "" : "s", dest);

	uint written = 0, next = 0;
	if (!testmode)
		for (uint off = 0; off + 16 <= bank_size; off += 4)
			if (camelot_bank_valid (bank + off, bank_size - off))
				written += camelot_texbank_to_pngs (bank + off, bank_size - off, dest, stem, &next);

	FREE (decoded);
	FREE (raw);
	return testmode || written ? ERR_OK : ERR_INVALID_DATA;
}
