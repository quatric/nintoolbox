// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_PIKMIN_H
#define LIB_PIKMIN_H 1

#include "lib-nintendo.h"
#include "lib-model-glb.h"

// Pikmin 1 (GameCube/Wii) .mod model: a chain of {u32 type, u32 length} chunks
// (big-endian, each starting on a 0x20 boundary) ending with type 0xFFFF.
// Layout after the projectPiki decompilation (CC0). Geometry plus base textures: joints are
// baked into the vertices; only the first texture of each material is decoded.
bool IsPikminMod (const u8 *data, uint size);
enumError DecodePikminMod (const u8 *data, uint size, ccp out_path);

#endif
