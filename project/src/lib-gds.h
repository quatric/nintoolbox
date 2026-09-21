// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Collision Studios "GDS" resource envelope (.PS2, and embedded inside other
// containers under this engine -- e.g. nested inside .pk2/PAK archives and
// inside .PSD files; "Brave: A Warrior's Tale", Wii).
//
// Fixed 0x50-byte header, all fields big-endian:
//   0x00  "GDS" + u8 version (8 in every sample seen)
//   0x04  16 zero bytes
//   0x14  u32 entry_count + 1
//   0x18  12 zero bytes
//   0x24  u32 unknown (not a simple function of file size)
//   0x28  u32 entry_count (one less than the 0x14 field)
//   0x2c  four u32, always 0x50 in every sample (the header size, repeated)
//   0x3c  u32 unknown offset
//   0x40  u32 constant 0x180c1010 (format/revision marker?)
//   0x44  u32 trailing-section size
//   0x48  u32 trailing-section offset (== file size - the 0x44 field)
//   0x4c  u32 total file size
//   0x50  entry_count * 8-byte records: {u32 name, u32 index (1-based)};
//         `name` is a 1-4 char identifier stored byte-reversed (a
//         self-referential first entry's name is the file's own name,
//         e.g. GAME.PS2's is "emag" -> "game").
//
// What follows the name table (up to the 0x4c total-size field) is the
// actual object/resource tree and was NOT reverse-engineered -- carved out
// as-is (payload.bin) so a later pass can re-scan it for nested containers
// this project already knows how to decode (it may itself start with a
// recognizable magic).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_GDS_H
#define SZS_LIB_GDS_H 1

#include "lib-std.h"

enumError ExtractGDSArchive (ccp arg, ccp basedir, uint depth);

#endif
