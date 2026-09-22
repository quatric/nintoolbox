// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Konami Bemani engine "ZMB" skinned character/prop model (Dance Dance
// Revolution: Winx Club, Wii, DATA/files/character/*.bin and
// DATA/files/accessory/*.bin; big-endian).
//
// See the comment above DecodeZMB() in lib-zmb.c for the chunk/bone/submesh
// layout, which was recovered by decompiling the loader in the disc's
// sys/main.dol (not guessed from data alone) and cross-checked against
// several retail samples.
//-----------------------------------------------------------------------------

#ifndef LIB_ZMB_H
#define LIB_ZMB_H

#include "types.h"

// Return true if 'data' begins with a "WII\0" wrapper around a "ZMB GC"
// chunk at the offset the wrapper header names (chunk offset, not
// necessarily 0x20, though that is what every sample seen uses).
bool IsZMB (const u8 *data, uint size);

// Decode every "WII\0"-wrapped "ZMB GC" chunk found back to back in 'data'
// (a character file is body+head, two chunks; a prop file is one chunk) and
// write a YAML text dump of textures, materials, bones (name, type,
// bind-pose transform / anchor point, submesh count) to 'out_path', plus a
// sibling Wavefront .obj (same path with its extension replaced by ".obj")
// containing the actual triangulated geometry -- positions, normals and UVs,
// one object per bone/submesh -- fan-triangulated from each vertex block's
// N-gon. Vertex colours are read but not written (OBJ has no standard vertex
// colour). Geometry is in the file's own untransformed pool coordinates, not
// posed through the bone hierarchy (parent-relative bind-pose composition
// and per-vertex skin-index assignment are not decoded -- see lib-zmb.c).
// Returns ERR_NOTHING_TO_DO if 'data' isn't a ZMB file.
enumError DecodeZMB (const u8 *data, uint size, ccp out_path);

#endif // LIB_ZMB_H
