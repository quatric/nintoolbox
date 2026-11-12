// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_BAEV_H
#define SZS_LIB_BAEV_H 1

#include "lib-std.h"

// Animation event archives (".baev", AnimationEventArchiveResData in a BFFH container). Little
// endian, 64-bit offsets, all absolute.
//
// BFFH header: magic, u32 0, u32 file size, u32 alignment, {u64 offset, u32 count, u32 size}
// section headers (BFSI: magic, u32 offset, u32 size, u32 alignment, u64 base, char name[16];
// "Default" and "StringPool"), u64 container, char type[0x80]. Container (at the "Default" base):
// u64, u8 sub, u8 minor, u16 major version, u32, u64 string pool, then two arrays {u64 offset, u32
// count, u32 entry size}:
//   animations: {u32 FNV-1a hash of the animation name, u32, {u64, u32 count, u32 4} node indices}
//   nodes:      {u64 events, u32 count, u32 0x30, u32 hash (ASB node), u32}
// Event (0x30): u64 name, {u64, u32, u32 0x18} triggers, {u64, u32, u32 0x18} holds, u32 hold flag,
// u32 id. Trigger: {u64, u32 count, u32 8} parameter pointers, f32 frame, f32 0; hold: the same
// with f32 start, f32 end. Parameter: u32 type, u32 0, then u32 (0 int), f32 (1 float), 3 x f32 (3
// vector) or u64 string (5).
//
// NAMES (optional, may be 0): any bytes holding the animation names (e.g. the matching animation
// BFRES); printable runs are hashed and matching animations get their names.

bool IsBaev (const u8 *data, size_t size);
enumError ConvertBaevToYaml (const u8 *data, size_t size, const u8 *names, size_t names_size,
	char **text, size_t *text_size);

#endif // SZS_LIB_BAEV_H
