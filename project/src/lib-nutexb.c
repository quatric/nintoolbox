#include "lib-std.h"
#include "lib-nutexb.h"
#include "lib-bntx.h"
#include <string.h>
#include <errno.h>

enumError DecodeNUTEXB_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	if (!dest || !width || !height || !src || src_size < 0x70)
		return EINVAL;
	if (memcmp (src + src_size - 7, "XET", 3))
		return EINVAL;

	// Header fields block: 0x28 (40) bytes starting 0x30 (48) bytes before
	// EOF -- i.e. "reader.Seek(pos - 48)" in NUTEXB.cs's Read().
	const u8 *hdr = src + src_size - 0x30;
	const u32 w = rd_le32 (hdr + 0x04);
	const u32 h = rd_le32 (hdr + 0x08);
	const u32 nutfmt = rd_le16 (hdr + 0x10);
	const u32 mip_count = rd_le32 (hdr + 0x18);
	const u32 image_size = rd_le32 (hdr + 0x24);
	if (!w || !h || !mip_count || (u64)image_size > src_size)
		return EINVAL;

	uint bntx_fmt = 0, bntx_type = 1, blk_h = 1;
	switch (nutfmt)
	{
		case 0x0400:
			bntx_fmt = 0x0b;
			break; // R8G8B8A8_UNORM
		case 0x0405:
			bntx_fmt = 0x0b;
			break; // R8G8B8A8_SRGB
		case 0x0450:
			bntx_fmt = 0x0c;
			break; // B8G8R8A8_UNORM
		case 0x0455:
			bntx_fmt = 0x0c;
			break; // B8G8R8A8_SRGB
		case 0x0480:
			bntx_fmt = 0x1a;
			blk_h = 4;
			break; // BC1_UNORM
		case 0x0485:
			bntx_fmt = 0x1a;
			blk_h = 4;
			break; // BC1_SRGB
		case 0x0490:
			bntx_fmt = 0x1b;
			blk_h = 4;
			break; // BC2_UNORM
		case 0x0495:
			bntx_fmt = 0x1b;
			blk_h = 4;
			break; // BC2_SRGB
		case 0x04a0:
			bntx_fmt = 0x1c;
			blk_h = 4;
			break; // BC3_UNORM
		case 0x04a5:
			bntx_fmt = 0x1c;
			blk_h = 4;
			break; // BC3_SRGB
		case 0x0180:
			bntx_fmt = 0x1d;
			blk_h = 4;
			break; // BC4_UNORM
		case 0x0185:
			bntx_fmt = 0x1d;
			blk_h = 4;
			bntx_type = 2;
			break; // BC4_SNORM
		case 0x0280:
			bntx_fmt = 0x1e;
			blk_h = 4;
			break; // BC5_UNORM
		case 0x0285:
			bntx_fmt = 0x1e;
			blk_h = 4;
			bntx_type = 2;
			break; // BC5_SNORM
		case 0x04d7:
			bntx_fmt = 0x1f;
			blk_h = 4;
			break; // BC6_UFLOAT
		case 0x04d8:
			bntx_fmt = 0x1f;
			blk_h = 4;
			bntx_type = 2;
			break; // BC6_SFLOAT
		case 0x04e0:
			bntx_fmt = 0x20;
			blk_h = 4;
			break; // BC7_UNORM
		case 0x04e5:
			bntx_fmt = 0x20;
			blk_h = 4;
			break; // BC7_SRGB
		default:
			// Includes R32G32B32A32_FLOAT (0x0434), which lib-bntx.c's
			// decoder has no equivalent for -- reported honestly rather
			// than guessed at.
			return ERROR0 (ERR_INVALID_IFORM,
				"Unsupported NUTEXB texture format 0x%04x\n", nutfmt);
	}

	// Same block-height-log2 derivation as EncodeBNTX_RGBA, generalized from
	// raw pixel height to element (block) height so it also covers the
	// BC-compressed formats above.
	const uint elem_h = (h + blk_h - 1) / blk_h;
	uint bh_log2;
	if (elem_h <= 16)
		bh_log2 = 0;
	else if (elem_h <= 32)
		bh_log2 = 1;
	else if (elem_h <= 64)
		bh_log2 = 2;
	else if (elem_h <= 128)
		bh_log2 = 3;
	else
		bh_log2 = 4;

	bntx_texture_t tex;
	memset (&tex, 0, sizeof (tex));
	tex.name = "nutexb";
	tex.width = w;
	tex.height = h;
	tex.format = bntx_fmt << 8 | bntx_type;
	tex.comp_sel = 0; // identity (R,G,B,A)
	tex.tile_mode = 0; // block-linear
	tex.block_height_log2 = bh_log2;
	tex.n_mips = 1;
	tex.data = src;
	tex.data_size = image_size;

	bntx_t bntx;
	memset (&bntx, 0, sizeof (bntx));
	bntx.data = src;
	bntx.size = src_size;
	bntx.n_textures = 1;
	bntx.textures = &tex;

	return DecodeBNTX_RGBA (dest, width, height, &bntx, 0);
}

static inline uint nut_round_up (uint val, uint align)
{
	return (val + align - 1) & ~(align - 1);
}

static inline uint nut_div_round_up (uint val, uint divisor)
{
	return (val + divisor - 1) / divisor;
}

static inline u64 nut_addr_block_linear (
	uint x, uint y, uint image_width, uint bytes_per_pixel, u64 base_address, uint block_height)
{
	const uint width_in_gobs = nut_div_round_up (image_width * bytes_per_pixel, 64);

	const u64 gob_address = base_address
		+ (u64)(y / (8 * block_height)) * 512 * block_height * width_in_gobs
		+ (u64)(x * bytes_per_pixel / 64) * 512 * block_height
		+ (u64)((y % (8 * block_height)) / 8) * 512;

	const uint xb = x * bytes_per_pixel;
	return gob_address + (u64)((xb % 64) / 32) * 256 + (u64)((y % 8) / 2) * 64
		+ (u64)((xb % 32) / 16) * 32 + (u64)(y % 2) * 16 + (xb % 16);
}

enumError EncodeNUTEXB_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, ccp name)
{
	if (!dest || !dest_size || !rgba || !width || !height)
		return EINVAL;

	if (!name || !*name)
		name = "texture";

	uint bh_log2;
	if (height <= 16)
		bh_log2 = 0;
	else if (height <= 32)
		bh_log2 = 1;
	else if (height <= 64)
		bh_log2 = 2;
	else if (height <= 128)
		bh_log2 = 3;
	else
		bh_log2 = 4;

	const uint block_height = 1u << bh_log2;
	const uint bpp = 4;
	const uint pitch = nut_round_up (width * bpp, 64);
	const uint surf_h = nut_round_up (height, block_height * 8);
	const u64 surf_size = (u64)pitch * surf_h;

	if (surf_size > 0x40000000)
		return EFBIG;

	const uint mip_section_size = 0x40;
	const uint trailer_size = 0x70;
	const u64 total_size = surf_size + mip_section_size + trailer_size;

	u8 *out = CALLOC (1, (size_t)total_size);
	if (!out)
		return ERR_CANT_CREATE;

	for (uint y = 0; y < height; y++)
		for (uint x = 0; x < width; x++)
		{
			const u64 pos = nut_addr_block_linear (x, y, width, bpp, 0, block_height);
			if (pos + bpp <= surf_size)
				memcpy (out + pos, rgba + 4 * ((size_t)y * width + x), 4);
		}

	// Mip section at offset surf_size
	u8 *mips = out + surf_size;
	wr_le32 (mips, (u32)surf_size);

	// Trailer at offset surf_size + mip_section_size
	u8 *tr = out + surf_size + mip_section_size;
	strncpy ((char *)tr + 4, name, 63);

	// Header block at tr + 0x40 (which is total_size - 0x30)
	u8 *hdr = tr + 0x40;
	wr_le32 (hdr + 0x04, width);
	wr_le32 (hdr + 0x08, height);
	wr_le32 (hdr + 0x0C, 1);
	wr_le16 (hdr + 0x10, 0x0400); // R8G8B8A8_UNORM
	wr_le32 (hdr + 0x18, 1);
	wr_le32 (hdr + 0x1C, 0x1000);
	wr_le32 (hdr + 0x20, 1);
	wr_le32 (hdr + 0x24, (u32)surf_size);

	// Magic & version at tr + 0x68 (total_size - 8)
	memcpy (tr + 0x68 + 1, "XET", 3);
	wr_le32 (tr + 0x68 + 4, 0x0200);

	*dest = out;
	*dest_size = (uint)total_size;
	return ERR_OK;
}

// CTPK (CTR Texture Package, 3DS container)
