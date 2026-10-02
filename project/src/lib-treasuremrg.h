#ifndef LIB_TREASURE_MRG_H
#define LIB_TREASURE_MRG_H

#include "file-type.h"
#include "dclib-types.h"

// Treasure Co., Ltd. Nintendo DS Multi-Resource Archive (*.mrg;
// Bleach: The Blade of Fate, Bleach: Dark Souls, Bangai-O Spirits; Nintendo DS)
//
// Format layout:
// +0x00..+0x03: u32 member count N (little-endian)
// +0x04..+0x04 + N * 8: array of N pairs { u32 offset, u32 size } (little-endian)
// followed by optional padding to 4- or 8-byte boundary, then member payloads.
// Member payloads include 2D screen maps/sprites (.bg4, .bg8), palettes, and scripts.

#define TREASURE_MRG_MAX_FILES 4096

bool IsTreasureMrg (const u8 *data, uint data_size, u64 file_size);

#endif // LIB_TREASURE_MRG_H
