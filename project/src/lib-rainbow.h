// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Rainbow Studios engine assets (Disney-Pixar Cars, GameCube / Wii).
//
// Recovered from the game's unstripped ELF (GCNTextureMap::LoadFromContainer,
// LoadSurfaces, GCNPalette::Read). All fields big-endian.
//
// .gct texture:
//   u32 2 (flags), u32 format, u32 palette entries
//   format 58: 256 RGB5A3 palette entries follow (CI8 image)
//   format 41: CMPR image, no palette
//   u32 surface count, u32 width, u32 height, then per surface (stored from the
//   smallest mip to the largest): u32 w, u32 h, u32 size, size bytes of plain
//   GX tiled data.
//
// .gcg geometry ("gcg\0", version 5, count 1): name[0x80] at 0x0c, 4x4 matrix
// at 0x8c, 7 bounds floats, 0xffffffff, u32 material count at 0xec, then
// material names (0x40 each) from 0xf0, then u32 flags(1), u32 colour, u32
// strip count and per strip: u32 material index followed by a "strip item":
//   u8 flags (bit0 = per-vertex matrix index, bit1/2 = UV sets, bit4 implied
//   = colour array unless bit0), then GX attribute descriptions
//   [pos: u8 vcd(2=idx8,3=idx16), u8 type, u8 frac] [colour: u8 vcd, u8 type]
//   [uv: u8 vcd, u8 type, u8 frac], then per array (u16 count, u8 stride) for
//   pos, colour, uv, u32 display list size, the three arrays, and the display
//   list (GX strips of indexed vertices). Materials name a .gcm ini file whose
//   TextureMap_1 names the .gct texture.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_RAINBOW_H
#define SZS_LIB_RAINBOW_H 1

#include "types.h"
#include "lib-model-glb.h"

bool IsGCT (const u8 *data, uint size);

// Decode the largest mip level to RGBA8 (dclib allocated).
enumError DecodeGCT (u8 **rgba, uint *width, uint *height, const u8 *data, uint size);

// Resolve a material name to a PNG file name (NULL = untextured).
typedef ccp (*RainbowTexFunc) (void *ctx, ccp material);

bool IsRainbowGCG (const u8 *data, uint size);
model_t *ParseRainbowGCG (const u8 *data, uint size, RainbowTexFunc texname, void *ctx);

#endif
