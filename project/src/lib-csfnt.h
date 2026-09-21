// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Collision Studios bitmap font metrics (.fnt, "Brave: A Warrior's Tale").
// Magic "TNF." (".FNT" byte-reversed, the same convention as this engine's
// other tagged formats).
//
// Layout, all fields big-endian:
//   0x00  "TNF."
//   0x04  u32 version (1 in every sample)
//   0x08  u16 first_used_char (32, ' ', in every sample -- informational)
//   0x0a  u16 used_count
//   0x0c  256-byte char-code -> glyph-index table (one byte per possible
//         8-bit character code; 0xff = no glyph)
//   0x10c u32 self-referential pointer, always exactly 0x10c + 4 (skipped)
//   0x110 used_count * 12-byte glyph metrics records:
//           u16 x_start, u16 y_start, u16 x_end, u16 height,
//           u16 reserved (0), u16 advance
//         Records are packed left-to-right in a texture strip: record[i+1]
//         .x_start == record[i].x_end, except across a row wrap, where
//         y_start increases instead. The texture atlas itself is not part
//         of this file (whatever .tpl/.png holds the font page is
//         separate and was not identified).
//
// Verified byte-exact against all 3 .fnt samples on the disc: header
// size + 256 + 4 + used_count*12 equals the file size exactly, with no
// leftover bytes, for used_count of 216 and 246.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_CSFNT_H
#define SZS_LIB_CSFNT_H 1

#include "lib-std.h"

enumError ExtractCSFontArchive (ccp arg, ccp basedir, uint depth);

#endif
