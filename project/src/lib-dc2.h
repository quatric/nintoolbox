// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "DC2" engine assets (Jakers! Kart Racing, Wii).
//
// .dcx archive (little more than a directory), all fields big-endian:
//   u32 n_entries
//   n_entries * { u32 name_len, char name[name_len] (backslash separators),
//                 u32 offset (absolute), u32 size }
//   member data follows the directory, back to back.
//
// .dct texture: "DC2\0" then a stream of big-endian u32 fields that start
// at offset 3 (so they sit at 4k+3):
//   0x1b width, 0x1f height, 0x23/0x27 width/height again, 0x2b mip count,
//   0x33.. three wrap/filter bytes, 0x3a u32 size of the top mip, image
//   data from 0x3e (Wii GX tiled: CMPR when the top mip is 4 bits per
//   pixel, RGBA8 at 32 bits per pixel; smaller mips and a ~28..140 byte
//   serialised-sampler trailer follow and are ignored). Animated textures
//   stack several frames; only the first is decoded.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_DC2_H
#define SZS_LIB_DC2_H 1

#include "lib-nintendo.h"

enumError ScanDCX (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

bool IsDCT (const u8 *data, uint size);
enumError DecodeDCT (u8 **rgba, uint *width, uint *height, const u8 *data, uint size);

#endif
