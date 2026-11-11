// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_AINB_H
#define SZS_LIB_AINB_H 1

#include "lib-std.h"

// AI node graph (".ainb", magic "AIB ", versions 0x404..0x408; Splatoon 3, Tears of the Kingdom, Mario Kart World
// 0x407). Layout as documented by the EPD wiki / TKVSC-Team's AINB tools:
//   0x00 "AIB ", u32 version, u32 file name, u32 command / node / query / attachment / output counts,
//   u32 blackboard offset, u32 string pool offset, then 19 u32: enum resolve, properties, transitions, IO params,
//   multi params, attachments, attachment indices, expressions, replacements, queries, 3 unknown, modules,
//   category name, category, actions, unknown, blackboard id. Commands (0x18 bytes: name, GUID, u16 root node,
//   u16 secondary root + 1) start at 0x74 and are followed by the nodes (0x3C bytes in 0x407: u16 type, s16 index,
//   u16 attachment count, u8 flags, name, [hash], unknown, body offset, expression counts, multi param count,
//   attachment base, query base/count, state info offset, GUID). A node body gives (base, count) slices of the
//   global property and input/output tables per parameter type (int, bool, float, string, vec3f, pointer) and
//   the plug table (each plug starts with {s32 node index, name}). All strings are offsets into the pool.
bool IsAINB (const u8 *data, size_t size);

// YAML description (MALLOC'd, free with FREE). Selector plug conditions and expression bytecode are not decoded.
enumError ConvertAINBToYaml (const u8 *data, size_t size, char **text, size_t *text_size);

#endif // SZS_LIB_AINB_H
