// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_ASB_H
#define SZS_LIB_ASB_H 1

#include "lib-std.h"

// Animation sequence binaries (".asb", magic "ASB ", version 0x41B), little endian, u32 absolute
// offsets, strings as u32 offsets into the string pool. Layout per the EPD wiki "ASB" article, with
// these corrections found on Tomodachi Life: Living the Dream data: Simultaneous and Event element
// plugs start at +0x04 (not +0x08), plug data offsets are absolute (not relative to the plug
// table), a PreviousTagSelector child plug is 8 bytes (tag array pointer, child id) and the float
// property ex pair at 0x48 is {pointer, count}.
//
// Header (0x80): name, command count/array (0x30 each: name, tag array, Property<float> frame
// count, u32, Property<u32> interpolation, GUID, u16 root element), element count/array (0x24 each:
// u16 type, u8 attachment count, u8 no-state-end, tag array, param pointer, u16 float-ex flags and
// count, u16 attachment base, u16 external action, GUID), events (unused), partials (0x10),
// attachments (0x18) and their index array, blackboard, string pool, enum relocations, states,
// float property ex (0x20), bone blends, partial skeletons (0x0C, bones 8 bytes), string pool size,
// transitions, transition partial skeletons, tags, external actions (3 strings), EXB, transition
// group count, material blends ({name, f32 weight}).
//
// Element params are decoded per type (selectors, skeletal / material / shape / bone visibility
// animations, blenders, frame controllers, ...), followed by a plug table: six {u8 count, u8 base}
// pairs (state, unknown, child, exb, event, frame controller) and u32 absolute offsets to plug
// data. Child plugs carry the selector condition of the parent type and the child element index.
// Property<T> is {u32 flags, T value}: bit 31 = bound to an index (bit 24 EXB, bit 30 calculation
// cache, bit 25 command data, otherwise a blackboard index), else the literal value.
//
// State, transition and bone blend records are not documented; their counts are listed only.

bool IsAsb (const u8 *data, size_t size);
enumError ConvertAsbToYaml (const u8 *data, size_t size, char **text, size_t *text_size);

// Appends "<guid>\0" for every element GUID (the strings AsNode ".baev" archives hash) to a
// MALLOC'd buffer; returns the size (0 on failure).
size_t CollectAsbGuidStrings (const u8 *data, size_t size, u8 **out);

#endif // SZS_LIB_ASB_H
