// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Atomic Planet "PUB" packages (*WII.PUB, Wii; AMF Bowling: Pinbusters!).
//
// Everything is big-endian. The file is a nested pair of section headers
//   0x00  {1, 1, SIZE-0x20, 0, 0, 0x1c, 0x1c, 0}     root
//   0x20  {1, N, TABLE, 0, 0, N*28, N*28, 0}          object section
// followed by N objects (32-byte aligned, starting at 0x40) and, at file
// offset TABLE, N 28-byte table entries
//   {u32 name hash, u32 file offset, u16 class, u16 sub, u32 size, u32 size,
//    u32 0, u32 tag}
// CLASS 1 is a texture, 8 a mesh, 3/2 and 4/0 carry other resources.
// Texture object (all offsets relative to the object):
//   +0x18 u32 palette offset       +0x1c u16 palette entries
//   +0x20 u8 mip count, u8 GX format (9 C8, 6 RGBA8, 1 I8), u8 palette format
//         (0 IA8, 1 RGB565, 2 RGB5A3)
//   +0x24 {u32 offset, u32 size, u16 height, u16 width} per mip level
// A few packages (effects, strap, SFX) use a different, unparsed header.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_PUB_H
#define SZS_LIB_PUB_H 1

#include "lib-std.h"

typedef struct
{
	u32 hash;
	uint width, height, gx_format, pal_format, pal_count;
	const u8 *pixels, *palette;
	size_t pixel_size;
} pub_texture_t;

bool IsAtomicPub (const u8 *data, size_t size);

// Textures of a package; the array is malloc'd, points into DATA.
pub_texture_t *ListPubTextures (const u8 *data, size_t size, uint *count);

enumError DecodePubTexture (u8 **rgba, const pub_texture_t *t);

#endif
