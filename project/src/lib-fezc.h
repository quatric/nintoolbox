// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "FEZC" file packages (*.fez; Wii WiiWare titles built on the GVF engine, e.g.
// Yard Sale Hidden Treasures). No public documentation exists; the layout below
// was worked out from a retail package and every member verifies: each record
// inflates to exactly its recorded uncompressed size.
//
// All fields little-endian.
//   0x00 u32 magic       "FEZC"
//   0x04 u32 version     0x10000 seen
//   0x08 u32 files       number of members (N)
//   0x0c u32 names_off   start of the name table (0x28)
//   0x10 u32 records_off start of the record table = end of the name table
//   0x14 u32 data_off    start of member data = records_off + 32 * N
//   0x18.. header hash, not needed
//   names_off[]          N times { u32 padded_len, NUL-terminated path padded to
//                        padded_len }, e.g. "data/strings.gvf"
//   records_off[]        N times 32 bytes:
//          u32 flags     0x102 = zlib stream (sync-flushed, no final block),
//                        0 = stored
//          u32 offset    absolute member data offset
//          u32 csize     bytes stored at offset
//          u32 usize     uncompressed size
//          u8  md5[16]   digest of the member, not checked
//
// Extract-only.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_FEZC_H
#define SZS_LIB_FEZC_H 1

#include "lib-nintendo.h"

bool IsFezc (const u8 *data, size_t size);
enumError ScanFezc (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
