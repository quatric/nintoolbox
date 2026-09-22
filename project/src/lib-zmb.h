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
// bind-pose transform / anchor point, submesh count) and, per submesh, its
// vertex-block count and the position bounding box the game itself would
// compute for culling. Mesh topology (vertex-block index arrays into the
// shared position pool), UV coordinates and per-vertex bone-index skinning
// are read and counted but not yet exported as geometry -- see lib-zmb.c.
// Returns ERR_NOTHING_TO_DO if 'data' isn't a ZMB file.
enumError DecodeZMB (const u8 *data, uint size, ccp out_path);

#endif // LIB_ZMB_H
