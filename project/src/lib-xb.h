// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_XB_H
#define SZS_LIB_XB_H 1

#include "types.h"
#include <stdio.h>

// Nd Cube Binary XML format ("XB" magic, 0x5842).
// Used in Mario Party 10, Animal Crossing: amiibo Festival, Wii Party U, etc.
// Reference: MPLibrary/WiiU/BinaryXML.cs and WiiU/XB.cs.

bool IsXB (const u8 *data, size_t size);

// Decodes Nd Cube Binary XML into indented XML text.
enumError DecodeXB (FILE *out, const u8 *data, size_t size);

// Decodes Nd Cube Binary XML into an allocated null-terminated string.
enumError DecodeXB_String (char **out_str, size_t *out_size, const u8 *data, size_t size);

// Encodes XML text (as produced by DecodeXB) back to Nd Cube Binary XML.
// Canonical layout, not retail-identical: strings in first-appearance
// order, u16 offsets unless wide != 0 or an offset exceeds 0xFFFF (then
// u32). Parent elements always carry an empty value (the reference
// decoder prints a parent's value inside its open tag, which is
// ambiguous to reparse, so retail files using that quirk fail here
// instead of misdecoding). Returns malloc'd bytes in *dest.
enumError EncodeXB (u8 **dest, uint *dest_size, const char *xml, size_t xml_len, int wide);

#endif // SZS_LIB_XB_H
