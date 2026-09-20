// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Heavy Iron Studios "Good Engine" .ho packages (Ratatouille, WALL-E, Up,
// SpongeBob: Truth or Square; Wii / big-endian "HEL", little-endian "HEB").
// Layout after heavyironmodding.org and HiHoTool, all fields big-endian:
//   0x000 "HEL\x1a" header, 0x800 bytes; sector size 0x800
//   0x800 MAST table: 0x20-byte header + one 0x40-byte entry whose
//         startSector (+0x1c of the entry) locates the SECT table
//   SECT  0x20-byte header (count at +4) + count * 0x40-byte layer entries:
//           +0x04 u16 language id, +0x1c u32 start sector, +0x20 u32 size,
//           +0x38 u32 offset (from SECT) of the layer's PSL / PSLD meta
//   PSL   "PSL\0" u32 size, u32 n_slices, u32 0, then n * {type, start,
//         size, align}; type 0 = asset table at layer start + start:
//           u32 count, u32 -1, 24 bytes of 't', count * 0x20 entries
//           {u32 padded_size, u32 offset (from layer start), u32 size,
//            u32 align, u64 asset_id, u32 type_hash, u32 flags}
//   PSLD  "PSLD" u32 size, u32 count, u32 first entry offset; the layer data
//         holds count u32 entry sizes, then entries {u64 asset_id, u32 name
//         offset, ...} with the asset's debug name.
// Assets are written as [langNN/]name.<type-hash>; textures and their raw
// pixel blobs share a name and differ in type hash.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_HO_H
#define SZS_LIB_HO_H 1

#include "lib-nintendo.h"

// Texture pixel blob ("RawBlob" next to a 16-byte "Texture" reference): 0x20
// marker 0x0020af30, u32 version (1/2), u32 12, u32 header size (0x14/0x1c),
// then at 0x20 + header size u16 height, u16 width, u32 GX format (5 RGB5A3,
// 6 RGBA8, 14 CMPR); pixels (tiled, followed by mip levels) start at 0x60.
bool IsHoTexture (const u8 *data, size_t size);
enumError DecodeHoTexture (u8 **rgba, uint *width, uint *height, const u8 *data, size_t size);

enumError ScanHO (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
