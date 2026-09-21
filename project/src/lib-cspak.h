// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Collision Studios "PAK" archive (.pk2, Wii; "Brave: A Warrior's Tale").
// Magic is the literal ASCII bytes 'K','A','P','.', i.e. the extension
// "PAK" spelled backwards with a leading dot dropped (the same convention
// this engine uses elsewhere, e.g. ".col" files start "..LOC", ".idb"
// files start "BDI.").
//
// Layout, all fields big-endian:
//   0x00  "KAP."
//   0x04  u32 version (2 or 3 seen)
//   0x08  u32 entry_count
//   0x0c  u32 table_offset (0x10 in every sample seen)
//   table: entry_count * 28-byte records:
//     +0x00 u32 data_offset  -- absolute file offset of this entry's data
//     +0x04 u32 hash         -- CRC/hash of the name, not verified
//     +0x08 char name[20]    -- relative path, '\'-separated, NUL padded;
//                               truncated from the FRONT if it doesn't fit
//                               (short names still NUL-terminate inside
//                               the field)
//   A record's data size is (next record's data_offset - this data_offset),
//   or (file size - data_offset) for the last record.
//
// Each entry's data itself begins with a redundant copy of (a suffix of)
// its own path as a NUL-terminated ASCII label, then MSVC-style 0xCD
// uninitialized-heap padding up to the real payload, which is whatever the
// name's extension implies (GDS container, FSB4 bank, ...). Established
// against default.pk2 (21 entries, /Volumes/.../DATA/files) by brute-forcing
// the record stride against the one invariant that must hold for any
// correct stride: strictly increasing, in-bounds data_offset fields whose
// data starts with that same redundant path label.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_CSPAK_H
#define SZS_LIB_CSPAK_H 1

#include "lib-std.h"

enumError ExtractCSPakArchive (ccp arg, ccp basedir, uint depth);

#endif
