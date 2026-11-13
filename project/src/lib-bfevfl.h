// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_BFEVFL_H
#define SZS_LIB_BFEVFL_H 1

#include "lib-std.h"

// Nintendo event flow binaries (".bfevfl": "BFEVFL\0\0", versions 0x0300 / 0x0400; Breath of the
// Wild, Tears of the Kingdom, Link's Awakening, the Switch 2 Zelda titles). Little endian, every
// pointer a 64-bit absolute file offset; strings are pascal strings (u16 length, bytes, NUL). Layout
// after the zeldamods "evfl" library:
//   header 0x48: magic[8], u16 version, u8, u8, u16 BOM 0xFEFF, u8 alignment shift, u8, u32 name
//     offset, u16 relocated, u16 first block, u32 relocation table, u32 file size, u16 flowchart count
//     (0/1), u16 timeline count (0/1), u32, u64 flowchart pointer array, u64 flowchart DIC, u64
//     timeline pointer array, u64 timeline DIC.
//   flowchart: "EVFL", u32 string pool, u32, u32, u16 actors/actions/queries/events/entry points,
//     3 x u16, u64 name, u64 actors, u64 events, u64 entry point DIC, u64 entry point array.
//   actor (0x38): {u64 name, u64 sub name}, u64 argument name, u64 actions, u64 queries, u64 params,
//     u16 actions, u16 queries, u16 argument entry point, u16 concurrent clips.
//   event (0x30; 0x28 in 0x0300): u64 name, u8 type (0 action, 1 switch, 2 fork, 3 join, 4 sub
//     flow, 5 sub flow switch, 6 return; the last two only in 0x0400), u8, then per type: action {u16 next, actor, action, u64 params}; switch {u16 cases,
//     actor, query, u64 params, u64 cases {u32 value, u16 event, u16}}; fork {u16 forks, join, 0, u64
//     u16[] forks}; join {u16 next}; sub flow {u16 next, 0, 0, u64 params, u64 flowchart, u64 entry
//     point}; sub flow switch {u16 cases, 0, 0, u64 params, u64 cases, u64 flowchart, u64 entry point}; return
//     {u16, s32 value}.
//   entry point (0x20): u64 sub flow indices, u64 variable DIC, u64 variable defs, u16 sub flows, u16
//     defs, u16 main event, u16; variable def (0x10): value (s32 / f32, or u64 to the array), u16 count,
//     u8 type.
//   container: {u8 tag, u8, u16 items, u32, u64 DIC}, u64 item offsets, items {tag, pad, count, ...}
//     with tag 2 int, 3 bool (0x80000001), 4 float, 5 string, 7/8/9/10 arrays, 12 actor identifier.
// The binary is dumped as YAML.

bool IsBfevfl (const u8 *data, size_t size);
enumError ConvertBfevflToYaml (const u8 *data, size_t size, char **text, size_t *text_size);

#endif // SZS_LIB_BFEVFL_H
