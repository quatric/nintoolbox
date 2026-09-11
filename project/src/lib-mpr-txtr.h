// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Metroid Prime Remastered (Switch) TXTR textures.
//
// TXTR form v47/51, little-endian: a HEAD chunk with STextureHeader
// (kind/format/width/height/layers/tile_mode/swizzle/mip_count +
// mip_sizes[] + sampler data) and a GPU chunk holding LZSS-compressed
// (u32 LE mode 0-3) buffers. Buffer assembly runs off the FOOT META
// table the PACK extractor appends (STextureMetaData: read infos +
// buffer descriptors with destination offsets), exactly like
// PrimeDecomp/retrotool's txtr.rs (MIT/Apache-2.0; re-implemented
// here, not copied).
//
// Detiling is Tegra X1 block-linear (GOB) math, verified element count
// against ~7000 retail META sizes: tile_mode/swizzle are constant zero
// across the corpus and ignored, like the reference. The per-mip block
// height schedule (inferred Sixteen→One, halved per level) and the
// inter-layer alignment match tegra_swizzle (MIT, Ryujinx-derived).
// Pixel codecs reuse this tree's BNTX/GTX/ASTC/BCn decoders.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_MPR_TXTR_H
#define SZS_LIB_MPR_TXTR_H 1

#include "types.h"

#define MPR_TXTR_MAX_DIM 16384
#define MPR_TXTR_MAX_MIPS 32
#define MPR_TXTR_MAX_LAYERS 2048
#define MPR_TXTR_MAX_OUTPUT (512u << 20)

typedef struct mpr_txtr_info_t
{
	uint kind; // 1 = 2D (arrays/cubes ride the same layer loop)
	uint format; // retrotool ETextureFormat id
	uint width;
	uint height;
	uint layers;
	uint tile_mode; // parsed, must be 0 (block-linear)
	uint mip_count;
} mpr_txtr_info_t;

// HEAD-only probe: RFRM/TXTR form (v47/51) with a parseable HEAD.
// Enough for DetectNintendoFormat(); full META validation happens in
// ScanMPRTXTR, so short probes still classify.
bool IsMPRTXTR (const u8 *data, uint size);

// Full parse: HEAD fields + FOOT META table + GPU buffer directory.
// Borrowed pointers stay valid while DATA lives.
enumError ScanMPRTXTR (mpr_txtr_info_t *info, const u8 *data, uint size);

// Decode mip 0 / layer 0 to tightly packed width*height RGBA8.
// Supports the corpus formats (R8/RGB8/RGBA8, BC1-5, BC6H/BC7, all
// ASTC footprints, R16/R32/RG16F/RGBA16F/RGBA32F/R11G11B10F/RGB10A2/
// RG8/RGBA16U + depth visualization); 3D, multisampled and exotic
// integer kinds fail cleanly.
enumError DecodeMPRTXTR_RGBA (
	u8 **dest, uint *width, uint *height, const u8 *src, uint src_size);

#endif
