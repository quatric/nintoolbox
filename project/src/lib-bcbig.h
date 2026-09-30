// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Blue Castle Games ".big" archives (The Bigs, The Bigs 2, Wii). No public
// documentation exists; the layout below was worked out from the 836 archives
// on the retail disc of The Bigs (USA), and every one of them satisfies it
// exactly (header size field == file size, every entry inside the file).
//
// All fields little-endian. Not to be confused with the Electronic Arts
// "BIGF" archives (see lib-bigf.h), which are big-endian and start with
// "BIGF".
//   0x00 u32 magic       0x01020304 (bytes 04 03 02 01)
//   0x04 u32 data_start  offset of the first member's data
//   0x08 u32 file_size   total archive size
//   0x0c u32 count       number of entries
//   0x10 u32 table       offset of the entry table (0x18 seen)
//   0x14 u32 names       offset of the NUL-separated name table
//   table: entry[count], 20 bytes each:
//          u32 name      offset of the name, absolute
//          u32 size      member size in bytes
//          u32 offset    absolute data offset
//          u32 kind      4 and 256 = nested archive, 32 = model/vertex
//                        blob, 2048 = 2048-aligned raw audio; storage is
//                        always plain (no compression)
//          u32 0
// Members are stored uncompressed. Nested archives are ordinary .big files
// that extract again in turn.
//
// Audio members are ".dspi": one standard 0x60-byte DSP header per channel
// back to back, then the channels' ADPCM data, each padded to 8 bytes (the
// last one is stored unpadded). The headers of a channel set are identical
// apart from the coefficients and history. 4264 such members across the
// audio archives of The Bigs (3708 stereo, 556 mono) all match this size
// rule exactly.
//
// Extract-only.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_BCBIG_H
#define SZS_LIB_BCBIG_H 1

#include "lib-nintendo.h"

bool IsBcBig (const u8 *data, size_t size);
bool IsBcDspi (const u8 *data, size_t size);
enumError ScanBcDspi (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);
enumError ScanBcBig (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
