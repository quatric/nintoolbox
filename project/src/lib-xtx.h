// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_XTX_H
#define LIB_XTX_H 1

#include "lib-nintendo.h"

// Nintendo Switch XTX Texture Container (.xtx / DFvN)
//
// Layout verified field-by-field against KillzXGaming/Switch-Toolbox,
// File_Format_Library/FileFormats/Texture/XTX.cs (the closest thing to a
// spec this format has): "DFvN" header (u32 header_size, major, minor),
// then "HBvN" blocks (u32 block_size, u64 data_size, s64 data_offset
// relative to the block start, u32 block_type + indices). Block type 2 is
// a fixed 120-byte texture header, block type 3 is that texture's Tegra
// block-linear pixel payload; header[i] pairs with payload[i] in order.
// Pixel decoding reuses the BNTX pipeline (same GPU, same swizzle).
enumError ExtractXTXArchive (ccp arg, ccp basedir, uint depth);

// One texture inside an XTX container.
typedef struct xtx_texture_t
{
	uint width, height;
	uint depth;
	uint target;
	uint format; // raw NVN image-format word (XTXImageFormat in XTX.cs)
	uint mip_count;
	uint slice_size;
	uint mip_offsets[17];
	uint block_height_log2; // TextureLayout1 & 7
	const u8 *data; // swizzled level-0..n payload of array slice 0
	uint data_size;
} xtx_texture_t;

typedef struct xtx_t
{
	const u8 *data;
	uint size;
	uint header_size;
	uint version_major, version_minor;
	uint n_textures;
	xtx_texture_t *textures; // owned
} xtx_t;

enumError ScanXTX (xtx_t *xtx, const u8 *data, uint size);
void ResetXTX (xtx_t *xtx);

// Decodes texture INDEX (and mip level) to tightly packed RGBA8. Only
// array slice 0 is decoded -- the same single-slice scope as
// DecodeBNTX_RGBA and DecodeNUTEXB_RGBA.
enumError DecodeXTX_Mip_RGBA (
	u8 **dest, uint *width, uint *height, const xtx_t *xtx, uint index, uint mip_level);
enumError DecodeXTX_RGBA (u8 **dest, uint *width, uint *height, const xtx_t *xtx, uint index);

// Human-readable name for a raw NVN format word ("" for unknown).
ccp GetXTXFormatName (uint format);

#endif // LIB_XTX_H
