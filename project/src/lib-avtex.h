// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Avalanche Software engine textures (Disney-Pixar Cars 2, Wii): a 48-byte
// big-endian .thb header next to a .tbb pixel file.
//   0x00 u32 1 (texture count), 0x04 u32 0x10, 0x08 u32 0
//   0x0c u32 tbb size, 0x10 u32 base level bytes, 0x14 u32, 0x18 u32
//   0x1c u16 width, u16 height, 0x20 u16 GX format, u16 mip count, then a
//   repeat of width/height and sizes.
// The .tbb starts with the tiled GX base level (5 RGB5A3, 6 RGBA8, 1 I8,
// 14 CMPR, ...); mip levels and (for CMPR) a duplicate copy follow.
// Headers with more than one texture (0x04 != 0x10) are not decoded.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_AVTEX_H
#define SZS_LIB_AVTEX_H 1

#include "lib-nintendo.h"

bool IsAvalancheTexHeader (const u8 *thb, size_t thb_size);
enumError DecodeAvalancheTex (u8 **rgba, uint *width, uint *height, const u8 *thb, size_t thb_size,
	const u8 *tbb, size_t tbb_size);

#endif
