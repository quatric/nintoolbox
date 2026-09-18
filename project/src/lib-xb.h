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

#endif // SZS_LIB_XB_H
