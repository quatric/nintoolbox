// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_GEN5TEXT_H
#define SZS_LIB_GEN5TEXT_H 1

#include "lib-std.h"

// Pokemon Black / White / Black 2 / White 2 message archives (the members of a/0/0/2 and a/0/0/3 in the NitroFS).
//
//   u16 blocks, u16 entries per block, u32 size of the biggest block, u32 0, u32 block offsets[blocks]
//   block: u32 size, entries x {u32 offset (from the block), u16 length in characters, u16 unknown}, then the strings
//          (UTF-16, each ends with 0xFFFF), the block padded to 4 bytes
//   every string is XORed with a key that starts at 0x7C89 + 0x2983 * entry and is rotated left by 3 after each character.
//
// The text dump has "@block N" lines and one "<entry>[:<unknown>]<TAB><text>" line per string. Characters outside the
// printable range (the 0xFFFE variable escapes, 0xF000 codes, ...) and the backslash are written as \uXXXX. The padding
// bytes of the original (arbitrary leftovers) are rewritten as zeros.

// A file that parses completely: every offset in range and every string ending in 0xFFFF.
bool IsGen5Text (const u8 *data, size_t size);
enumError DecodeGen5Text (const u8 *data, size_t size, char **text, size_t *text_size);
enumError EncodeGen5Text (const char *text, size_t text_size, u8 **bin, size_t *bin_size);

#endif // SZS_LIB_GEN5TEXT_H
