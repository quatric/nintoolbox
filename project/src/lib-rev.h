// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Blitz Games "Babel" .rev packages (Nickelodeon SpongeBob SquarePants:
// Creature from the Krusty Krab, Wii) -- Packages_Rev/*.rev and AudioRev/*.rev.
//
// Recovered from the game's own (unstripped) ELF: bOpenPackage,
// bFindIndexFileByCRC, bkLoadFileByCRC, bkStringLwrCRC, FixupActorNodeList,
// bFixupMeshData, bFixupSoftSkin and bUploadTexture. All fields big-endian.
//
// Container:
//   0x00 u32 id            0x04 u32 align (0x20 packages, 0x800 audio); every
//                                  offset below is in units of `align`
//   0x0c u32 num_files     0x10 u32 index offset
//   0x28 u32 names offset  0x2c u32 names length (NUL separated strings)
//   Index (32-byte entries, sorted by CRC): u32 offset, u32 name CRC,
//   u32 size, u32 size2, u32 1, u32 4, u64 FILETIME. Payloads are stored raw.
//   Name CRC = MSB-first CRC32 (poly 04C11DB7, init 0, no final xor) over the
//   lowercased name. The names table is in file order, not CRC order, so
//   names are matched to entries by CRC; unmatched entries become <crc>.bin.
//
// Texture resource (0xa0 byte header, zero for its first 0x20 bytes):
//   0x20 u32 width, 0x24 u32 height, 0x28 u32 format, 0x6c u32 palette
//   offset, 0x70 u32 pixel offset. Formats: 15 RGBA8, 16 RGB5A3, 17/18 CI8
//   (RGB565/RGB5A3 palette), 19/20 CI4 (RGB565/RGB5A3 palette), 21 CMPR,
//   22 I4, 23 RGB565, 29/30 I8 -- plain GX tiled data, mip levels follow.
//
// Actor resource (u32 0, u32 0x100 header; nodes/meshes are file offsets):
//   0xa0 root node, 0xa4 flags (bit0 = soft skinned). Node: +0x00 position,
//   +0x20 quaternion (xyzw), +0x50 scale, +0x70 type (2 = static mesh), +0x74
//   index, +0x110 next sibling (circular), +0x118 parent, +0x11c first child,
//   +0x80 embedded mesh. Static meshes carry their GX arrays and indexed
//   display lists in the node's mesh; soft skinned actors carry the same
//   thing in the actor header (the bind pose is exported, bones are not).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_REV_H
#define SZS_LIB_REV_H 1

#include "types.h"
#include "lib-model-glb.h"

typedef struct nintendo_sarc_entry_t nintendo_sarc_entry_t;

// Blitz name CRC (lowercases NAME).
u32 RevNameCRC (ccp name);

// Split a .rev container into a malloc-owned entry list (ResetOwnedEntries).
enumError ScanREV (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

// Recognise / decode a Blitz texture resource to RGBA8 (dclib allocated).
bool IsBlitzTexture (const u8 *data, uint size);
enumError DecodeBlitzTexture (u8 **rgba, uint *width, uint *height, const u8 *data, uint size);

// Recognise a Blitz actor resource, and build a model_t from it. TEXNAME maps a
// material's texture CRC to the sibling PNG name (NULL result = untextured).
typedef ccp (*BlitzTexNameFunc) (void *ctx, u32 crc);
bool IsBlitzActor (const u8 *data, uint size);
model_t *ParseBlitzActor (const u8 *data, uint size, BlitzTexNameFunc texname, void *ctx);

#endif
