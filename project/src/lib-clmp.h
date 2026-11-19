// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// War Drum "CLMP" model files (Manhunt 2 Wii, "global/*.dff"; big endian).
//
// The file is a relocatable image: a name table of bones, ".gpl" geometry
// pools and ".tpl" texture pools. Every geometry pool starts with a mesh
// block, found by its constant first word and checked through its position
// descriptor (all offsets are relative to the block start, B):
//   B+0x00  u32 0x18        offset of the position descriptor
//   B+0x04  u32 colour descriptor offset
//   B+0x08  u32 texcoord descriptor array offset (16-byte descriptors)
//   B+0x0c  u32 normal descriptor offset
//   B+0x10  u32 command list offset
//   B+0x14  u8  number of texcoord sets
// Array descriptors are {u32 offset, u16 count, u8 (type << 4 | fraction bits),
// u8 components}: positions and normals share one array of s16 entries
// (pos x y z, normal x y z, fraction 14, normals start at B+0x2a), texcoords
// are s16 pairs with 11 fraction bits. The command list is {u32, u32 entries
// offset, u16 count} with 16-byte entries {u8 type, 3 pad, u32 value, u32 display
// list offset, u32 display list size}: type 1 selects a material (value & 0x1fff),
// type 3 sets the vertex format (value = 2-bit index width per attribute from bit 2:
// position, normal, colour 0, colour 1, texcoord 0, 1, ...; 0 absent, 2 u8, 3 u16)
// and every non-zero display list offset names a GX display list (0x80 quads,
// 0x90 triangles, 0x98 strip, 0xa0 fan) drawn in that state. Vertex positions are
// already in model space (the body is a T-pose figure).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_CLMP_H
#define SZS_LIB_CLMP_H 1

#include "lib-std.h"
#include "lib-model-glb.h"

bool IsClmpModel (const u8 *data, size_t size);
model_t *BuildClmpModel (const u8 *data, size_t size);

#endif
