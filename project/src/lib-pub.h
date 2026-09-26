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
// Mesh object (class 8; all offsets relative to the object):
//   +0x00 f32 bounding box (min xyz, max xyz)   +0x70 offset of the root R
//   R+4 / R+8 / R+12 offsets of three lists of N u32 offsets, N = u16 at R+16:
//     A[i]: {u32 1|1<<16, u32 display list offset, u32 display list size, ..}
//     B[i]: {.., u32 texture name hash at +12 (up to two)}
//     C[i]: {u32 flag, u32 (slot pointer array), u16 slots<<16 ...}, the slot
//           pointer array starts at C+12; slot S = {flag, list, count<<16},
//           list = count offsets of attribute records
//           {u32 flag, u32 data, u16 count, u8 offset, u8 stride, u8 size, 3 x 0}.
//   Slots: 0 position (3 x f32), 1 colour (RGBA8), 2 texcoord (2 x s16 / 1024),
//   3 normal (3 x s16 / 16384 or 3 x f32). The display list is a run of
//   {u8 command (0x80 quads, 0x90 triangles, 0x98 strip, 0xa0 fan), u16 count,
//   count vertices}, a vertex holding one u16 index per present slot in the
//   order position, normal, colour, texcoord.
// A few packages (effects, strap, SFX) use a different, unparsed header.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_PUB_H
#define SZS_LIB_PUB_H 1

#include "lib-std.h"
#include "lib-model-glb.h"

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

// Name of the PNG for texture NAME HASH, or NULL.
typedef ccp (*PubTexFunc) (void *ctx, u32 hash);

// Meshes of a package: object at OFFSET, SIZE bytes. Returns NULL if unusable.
model_t *ParsePubMesh (const u8 *data, size_t size, PubTexFunc texname, void *ctx);

// Class 8 objects of a package: {name hash, offset, size}; malloc'd.
typedef struct
{
	u32 hash, offset, size;
} pub_object_t;
pub_object_t *ListPubMeshes (const u8 *data, size_t size, uint *count);

enumError DecodePubTexture (u8 **rgba, const pub_texture_t *t);

#endif
