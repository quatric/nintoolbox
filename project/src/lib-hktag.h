// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_HKTAG_H
#define SZS_LIB_HKTAG_H 1

#include "lib-std.h"

// Havok tag files (2016+ "TAG0" layout, e.g. Havok 2023.2 in Phive ".bphcl" cloth) dumped as YAML
// through their own reflection data. Section headers are {u32 big endian size | flags << 30, char
// tag[4]}: TAG0 { SDKV, DATA, TYPE { TPTR, TST1 type name strings, TNA1 types {name, templates},
// FST1 field name strings, TBDY bodies, THSH, TPAD }, INDX { ITEM {u32 flags << 24 | type, u32 data
// offset, u32 count}, PTCH } }. Integers in TNA1/TBDY are Havok varints (1-5 bytes, length in the
// top bits of the first).
//
// A TBDY body: type, parent, flags; 1: format, 2: subtype, 4: version, 8: size + alignment, 0x10:
// abstract value, 0x20: members {name, flags, [extra if flags & 0x80], offset, type}, 0x40:
// interfaces {type, value}. Format & 0x1f: 2 bool, 3 string, 4 int (0x200 signed), 5 float, 6
// pointer, 7 record, 8 array (0x20 = fixed size, count = format >> 8; otherwise an hkArray). DATA
// is little endian; pointers, strings and hkArrays hold ITEM indices (u64), a string item is a char
// array.
//
// Every object item (flags 0x10) is written with its type name and fields (parents' fields first);
// pointers appear as "@<item>".

bool IsHavokTagFile (const u8 *data, size_t size);
enumError ConvertHavokTagFileToYaml (const u8 *data, size_t size, char **text, size_t *text_size);

// Phive cloth (".bphcl", "Phive\0" 01 00, BOM, 03 03): u32 tag file offset (0x0C), u32 parameter
// offset, u32 file size, u32 tag file size, u32 parameter size; the parameters are an AAMP (type
// "pclt").
bool IsPhiveCloth (const u8 *data, size_t size, size_t *tag_off, size_t *tag_size, size_t *aamp_off,
	size_t *aamp_size);

#endif // SZS_LIB_HKTAG_H
