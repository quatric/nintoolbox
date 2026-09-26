// SPDX-License-Identifier: GPL-2.0+
// Split out of lib-nintendo-archives.c -- one archive format per file.
#include "lib-nintendo-archives.h"
#include "lib-nintendo.h"
#include "lib-xtx.h"
#include "lib-bntx.h"
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

// Extract Nintendo Switch XTX Texture Container (.xtx / DFvN)
enumError ExtractXTXArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".xtx") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || memcmp (raw, "DFvN", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// XTX Header (16 bytes):
	// 0x00: "DFvN"
	// 0x04: u32 HeaderSize
	// 0x08: u32 MajorVersion
	// 0x0C: u32 MinorVersion
	const u32 header_size = rd_le32 (raw + 4);
	if (header_size < 16 || header_size >= raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	uint block_pos = header_size;
	uint image_idx = 0;

	// Loop through blocks (HBvN)
	while (block_pos + 32 <= raw_size)
	{
		if (memcmp (raw + block_pos, "HBvN", 4))
			break;

		const u32 block_size = rd_le32 (raw + block_pos + 4);
		const u64 data_size = rd_le64 (raw + block_pos + 8);
		const s64 data_offset = (s64)rd_le64 (raw + block_pos + 16);
		const u32 block_type = rd_le32 (raw + block_pos + 24);

		// BlockType 3 is Texture Data block
		if (block_type == 3 && data_size > 0)
		{
			const s64 abs_payload = (s64)block_pos + data_offset;
			// data_size is a raw attacker u64; adding it to abs_payload can
			// itself overflow size_t and wrap below raw_size, so bound
			// abs_payload against raw_size first and subtract instead.
			if (abs_payload >= 0 && (size_t)abs_payload <= raw_size
				&& data_size <= raw_size - (size_t)abs_payload)
			{
				char out_file[PATH_MAX];
				snprintf (out_file, sizeof (out_file), "%s/texture_%04u.bin", dest, image_idx++);

				if (!testmode)
					SaveFile (out_file, 0, 0, raw + abs_payload, (uint)data_size, 0);
			}
		}

		if (block_size == 0)
			break;
		block_pos += block_size;
	}

	FREE (raw);
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		pixel decoding (via the BNTX pipeline)		///////////////
//-----------------------------------------------------------------------------

// XTX carries Tegra block-linear surfaces, exactly like BNTX -- only the
// header and the format-code spelling differ (NVN words, see XTXImageFormat
// in Switch-Toolbox's XTX.cs). Map each NVN word to BNTX's own (fmt,type)
// encoding and hand a synthetic one-texture bntx_t to DecodeBNTX_RGBA,
// the same bridge lib-nutexb.c already uses for NUTEXB.
static bool xtx_map_format (uint nvn, uint *bntx_fmt, uint *bntx_type)
{
	uint fmt = 0, type = 1;
	switch (nvn)
	{
		case 0x01:
			fmt = 0x02;
			break; // R8
		case 0x0d:
			fmt = 0x09;
			break; // RG8
		case 0x25:
			fmt = 0x0b;
			break; // RGBA8
		case 0x38:
			fmt = 0x0b;
			type = 6;
			break; // RGBA8_SRGB
		case 0x39:
			fmt = 0x03;
			break; // RGBA4 (no B-first word exists; see below)
		case 0x3b:
			fmt = 0x3b;
			break; // RGB5A1 (B5G5R5A1 in BNTX spelling)
		case 0x3c:
			fmt = 0x08;
			break; // RGB565 (B5G6R5 in BNTX spelling)
		case 0x3d:
			fmt = 0x0e;
			break; // RGB10A2
		case 0x42:
			fmt = 0x1a;
			break; // DXT1 / BC1
		case 0x43:
			fmt = 0x1b;
			break; // DXT3 / BC2
		case 0x44:
			fmt = 0x1c;
			break; // DXT5 / BC3
		case 0x49:
			fmt = 0x1d;
			break; // BC4U
		case 0x4a:
			fmt = 0x1d;
			type = 2;
			break; // BC4S
		case 0x4b:
			fmt = 0x1e;
			break; // BC5U
		case 0x4c:
			fmt = 0x1e;
			type = 2;
			break; // BC5S
		case 0x4d:
			fmt = 0x20;
			break; // BC7U
		case 0x50:
			fmt = 0x1f;
			break; // BC6U (unsigned: decoder signs iff type 2/0xb)
		case 0x6d:
			fmt = 0x0c;
			break; // BGRA8
		default:
			if (nvn >= 0x79 && nvn <= 0x86) // ASTC_*_UNORM
			{
				fmt = 0x2d + (nvn - 0x79);
				break;
			}
			if (nvn >= 0x87 && nvn <= 0x94) // ASTC_*_SRGB
			{
				fmt = 0x2d + (nvn - 0x87);
				type = 6;
				break;
			}
			return false;
	}
	*bntx_fmt = fmt;
	*bntx_type = type;
	return true;
}

ccp GetXTXFormatName (uint format)
{
	switch (format)
	{
		case 0x01:
			return "R8";
		case 0x0d:
			return "RG8";
		case 0x25:
			return "RGBA8";
		case 0x38:
			return "RGBA8_SRGB";
		case 0x39:
			return "RGBA4";
		case 0x3b:
			return "RGB5A1";
		case 0x3c:
			return "RGB565";
		case 0x3d:
			return "RGB10A2";
		case 0x42:
			return "DXT1";
		case 0x43:
			return "DXT3";
		case 0x44:
			return "DXT5";
		case 0x49:
			return "BC4U";
		case 0x4a:
			return "BC4S";
		case 0x4b:
			return "BC5U";
		case 0x4c:
			return "BC5S";
		case 0x4d:
			return "BC7U";
		case 0x50:
			return "BC6U";
		case 0x6d:
			return "BGRA8";
		default:
			if (format >= 0x79 && format <= 0x86)
				return "ASTC_UNORM";
			if (format >= 0x87 && format <= 0x94)
				return "ASTC_SRGB";
			return "";
	}
}

// 120-byte type-2 texture header, all fields little endian.
#define XTX_TEXHDR_SIZE 120
#define XTX_MAX_TEXTURES 4096

enumError ScanXTX (xtx_t *xtx, const u8 *data, uint size)
{
	if (!xtx || !data || size < 16 || memcmp (data, "DFvN", 4))
		return EINVAL;
	memset (xtx, 0, sizeof (*xtx));

	const u32 header_size = rd_le32 (data + 4);
	if (header_size < 16 || header_size > size)
		return EINVAL;
	xtx->data = data;
	xtx->size = size;
	xtx->header_size = header_size;
	xtx->version_major = rd_le32 (data + 8);
	xtx->version_minor = rd_le32 (data + 12);

	// First pass: collect type-2 headers and type-3 payloads in order.
	// Cap the walk so a corrupt block_size cannot loop forever.
	typedef struct xtx_blk_t
	{
		const u8 *payload;
		uint payload_size;
	} xtx_blk_t;
	xtx_blk_t *infos = 0, *images = 0;
	uint n_infos = 0, n_images = 0;
	uint block_pos = header_size;
	for (uint guard = 0; guard < 2 * XTX_MAX_TEXTURES + 16; guard++)
	{
		if (block_pos + 36 > size)
			break;
		if (memcmp (data + block_pos, "HBvN", 4))
			break;
		const u32 block_size = rd_le32 (data + block_pos + 4);
		const u64 data_size = rd_le64 (data + block_pos + 8);
		const s64 data_offset = (s64)rd_le64 (data + block_pos + 16);
		const u32 block_type = rd_le32 (data + block_pos + 24);
		if (!block_size)
			break;
		const s64 abs_payload = (s64)block_pos + data_offset;
		if (abs_payload < 0 || (u64)abs_payload > size || data_size > size - (u64)abs_payload)
			return EINVAL;
		if (block_type == 2 || block_type == 3)
		{
			xtx_blk_t **list = block_type == 2 ? &infos : &images;
			uint *n = block_type == 2 ? &n_infos : &n_images;
			if (*n >= XTX_MAX_TEXTURES)
				return EFBIG;
			xtx_blk_t *grown = REALLOC (*list, (*n + 1) * sizeof (**list));
			if (!grown)
			{
				FREE (infos);
				FREE (images);
				return ERR_CANT_CREATE;
			}
			*list = grown;
			(*list)[*n].payload = data + abs_payload;
			(*list)[*n].payload_size = data_size > UINT32_MAX ? UINT32_MAX : (uint)data_size;
			(*n)++;
		}
		if (block_size > size - block_pos)
		{
			FREE (infos);
			FREE (images);
			return EINVAL;
		}
		block_pos += block_size;
	}

	// Headers pair with payloads strictly in order (XTX.cs: curTex++).
	if (!n_infos || n_infos != n_images)
	{
		FREE (infos);
		FREE (images);
		return EINVAL;
	}

	xtx->textures = CALLOC (n_infos, sizeof (*xtx->textures));
	if (!xtx->textures)
	{
		FREE (infos);
		FREE (images);
		return ERR_CANT_CREATE;
	}
	for (uint i = 0; i < n_infos; i++)
	{
		if (infos[i].payload_size < XTX_TEXHDR_SIZE)
		{
			FREE (infos);
			FREE (images);
			ResetXTX (xtx);
			return EINVAL;
		}
		const u8 *h = infos[i].payload;
		xtx_texture_t *t = xtx->textures + i;
		const u64 hdr_datasize = rd_le64 (h);
		t->width = rd_le32 (h + 12);
		t->height = rd_le32 (h + 16);
		t->depth = rd_le32 (h + 20);
		t->target = rd_le32 (h + 24);
		t->format = rd_le32 (h + 28);
		t->mip_count = rd_le32 (h + 32);
		t->slice_size = rd_le32 (h + 36);
		for (uint m = 0; m < 17; m++)
			t->mip_offsets[m] = rd_le32 (h + 40 + 4 * m);
		t->block_height_log2 = rd_le32 (h + 108) & 7;
		t->data = images[i].payload;
		t->data_size = images[i].payload_size;
		uint map_fmt = 0, map_type = 1;
		if (!t->width || !t->height || !t->mip_count || !t->slice_size
			|| hdr_datasize > t->data_size || t->mip_offsets[0] > t->data_size
			|| t->block_height_log2 > 5 || !xtx_map_format (t->format, &map_fmt, &map_type))
		{
			// Unknown pixel formats are rejected here, not at decode
			// time, so AssignIMG() never claims an XTX it cannot draw.
			FREE (infos);
			FREE (images);
			ResetXTX (xtx);
			return EINVAL;
		}
	}
	xtx->n_textures = n_infos;
	FREE (infos);
	FREE (images);
	return ERR_OK;
}

void ResetXTX (xtx_t *xtx)
{
	if (xtx)
	{
		FREE (xtx->textures);
		memset (xtx, 0, sizeof (*xtx));
	}
}

enumError DecodeXTX_Mip_RGBA (
	u8 **dest, uint *width, uint *height, const xtx_t *xtx, uint index, uint mip_level)
{
	if (!dest || !width || !height || !xtx || index >= xtx->n_textures)
		return EINVAL;
	const xtx_texture_t *t = xtx->textures + index;
	if (mip_level >= t->mip_count || mip_level >= 17)
		return EINVAL;

	uint bntx_fmt = 0, bntx_type = 1;
	if (!xtx_map_format (t->format, &bntx_fmt, &bntx_type))
		return ERROR0 (ERR_INVALID_IFORM, "Unsupported XTX texture format 0x%08x\n", t->format);

	// Slice 0 only: mip offsets address bytes inside this texture's own
	// type-3 payload (XTX.cs GetImageData: base + MipOffsets[mip]).
	if (t->mip_offsets[mip_level] > t->data_size)
		return EINVAL;
	u64 *mip_offs = CALLOC (t->mip_count, sizeof (*mip_offs));
	if (!mip_offs)
		return ERR_CANT_CREATE;
	for (uint m = 0; m < t->mip_count && m < 17; m++)
		mip_offs[m] = t->mip_offsets[m] <= t->data_size ? t->mip_offsets[m] : t->data_size;

	bntx_texture_t syn;
	memset (&syn, 0, sizeof (syn));
	syn.name = "xtx";
	syn.width = t->width;
	syn.height = t->height;
	syn.format = bntx_fmt << 8 | bntx_type;
	syn.comp_sel = 0; // identity (R,G,B,A)
	syn.tile_mode = 0; // block-linear, like every Tegra surface
	syn.block_height_log2 = t->block_height_log2;
	syn.n_mips = t->mip_count;
	syn.data = t->data;
	syn.data_size = t->slice_size <= t->data_size ? t->slice_size : t->data_size;
	syn.mip_offsets = mip_offs;

	bntx_t bntx;
	memset (&bntx, 0, sizeof (bntx));
	bntx.data = t->data;
	bntx.size = t->data_size;
	bntx.n_textures = 1;
	bntx.textures = &syn;

	const enumError err = DecodeBNTX_Mip_RGBA (dest, width, height, &bntx, 0, mip_level);
	FREE (mip_offs);
	return err;
}

enumError DecodeXTX_RGBA (u8 **dest, uint *width, uint *height, const xtx_t *xtx, uint index)
{
	return DecodeXTX_Mip_RGBA (dest, width, height, xtx, index, 0);
}
