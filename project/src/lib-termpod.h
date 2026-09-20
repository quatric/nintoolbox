// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Terminal Reality style POD3/POD4/POD5 archives (WII*.POD in Nickelodeon
// Dance, Wii). Layout after termpod (github.com/jopadan/termpod), all fields
// little-endian:
//   0x000 char[4] "POD3" / "POD4" / "POD5", u32 header crc
//   0x058 u32 n_entries
//   0x108 u32 entry_offset, 0x110 u32 names_size
//   entries at entry_offset (POD3: 20 bytes, POD4/5: 28 bytes):
//     u32 name_offset (into the name table that follows the entries)
//     u32 size, u32 offset
//     [POD4/5: u32 uncompressed_size, u32 compression_level]
//     u32 timestamp, u32 crc
// Only stored members are extracted; compressed ones are skipped. Paths use
// backslashes and are converted to '/'.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_TERMPOD_H
#define SZS_LIB_TERMPOD_H 1

#include "lib-nintendo.h"

enumError ScanTermPod (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
