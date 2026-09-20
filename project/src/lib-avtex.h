// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Avalanche Software engine textures (Disney-Pixar Cars 2, Wii): a big-endian
// .thb header next to a .tbb pixel file holding one or more textures.
//   0x00 u32 n (texture count)
//   0x04 n * 12 bytes {u32 record offset, u32 tbb offset, u32 tbb bytes}
//   record (32 bytes, at its offset): u32 bytes, u32, u32,
//          u16 width, u16 height, u16 GX format, u16 mip count, u32 x3
// Each texture is a tiled GX image (0 I4, 1 I8, 2 IA4, 3 IA8, 4 RGB565,
// 5 RGB5A3, 6 RGBA8, 14 CMPR) followed by its mip levels; only the base level
// is decoded. For CMPR the data is stored twice; the first copy is used.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_AVTEX_H
#define SZS_LIB_AVTEX_H 1

#include "lib-nintendo.h"

// Returns the number of textures described by the header, or 0.
uint AvalancheTexCount (const u8 *thb, size_t thb_size);
bool IsAvalancheTexHeader (const u8 *thb, size_t thb_size);
enumError DecodeAvalancheTex (u8 **rgba, uint *width, uint *height, const u8 *thb, size_t thb_size,
	const u8 *tbb, size_t tbb_size, uint index);

#endif
