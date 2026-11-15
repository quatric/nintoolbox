// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_GRANNY_H
#define LIB_GRANNY_H 1

#include "lib-nintendo.h"

// Granny 2 (RAD Game Tools) ".gr2" files, big-endian 32-bit variant as used by Wii games (magic B5 95 11 0E 4B B5 A5
// 6A 50 28 28 EB 04 B3 78 25). Sections are stored uncompressed here; pointers come from the per-section relocation
// tables and the object layout from the embedded type definitions, so the file is read generically.
bool IsGrannyBE (const u8 *data, uint size);

// Writes a GLB of every mesh (vertex positions / normals / UV0 / colours, one primitive per material group, bind pose,
// no skinning). Textures are referenced by the material's diffuse map file name stem when a "<stem>.png" exists next to
// OUT_PATH.
enumError DecodeGrannyBE (const u8 *data, uint size, ccp out_path);

bool IsPetVetWiiTexture (const u8 *data, uint size);
enumError DecodePetVetWiiTexture (const u8 *data, uint size, ccp png_path);

// Generic JSON dump of the whole object tree (used for animation-only files).
enumError DumpGrannyBEJson (const u8 *data, uint size, ccp out_path);

#endif
