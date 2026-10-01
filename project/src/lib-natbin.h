// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Natsume "BIN\0" archives (Harvest Moon: Magical Melody, Tree of Tranquility
// and the other Wii Harvest Moons). Layout worked out from the 134 FSH_*/
// character animation archives on the retail disc of Magical Melody; every
// one satisfies the rules below. All fields little-endian.
//   0x00 char[4] magic   "BIN\0"
//   0x04 u16 version     1
//   0x06 u16 flags       1
//   0x08 u32 count
//   0x0c u32 0
//   0x10 entry[count], 16 bytes each: u32 offset, u32 size, u32 0, u32 0
// Members are stored plain. A member that starts with a short printable tag
// ("cdt", "mss") gets that as its extension, everything else is ".bin".
//
// Extract-only.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_NATBIN_H
#define SZS_LIB_NATBIN_H 1

#include "lib-nintendo.h"

bool IsNatBin (const u8 *data, size_t size);
enumError ScanNatBin (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
