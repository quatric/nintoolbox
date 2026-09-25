// SPDX-License-Identifier: GPL-2.0+
// Barking Lizards Technologies "pkg\0" script/data archive, as shipped on
// *Nickelodeon: The Naked Brothers Band - The Video Game* (Wii). No public
// documentation of this format (or the studio's "Whiptail"-nicknamed engine)
// was found on MobyGames, GitHub, xentax/Zenhax archives or Wii modding
// forums; reverse-engineered from scratch against all 256 `.pkg` files on
// the retail disc (scripts.pkg, actor/, menu/, song_sa1/, song_sa2/,
// song_nb/, wrld_ent/, gamebrd/, instrmnt/, venues/), cross-checking every
// entry's declared size/offset against the real gzip stream lengths and
// stored-byte spans byte-for-byte (0 mismatches across all 256 files).
//
// Layout, all integers big-endian:
//   Header (16 bytes):
//     char magic[4];       // "pkg\0"
//     u32  reserved;       // always 0 in every sample
//     u32  entry_count;
//     u32  version;        // always 1 in every sample
//   Stats block (32 bytes, 8 x u32), purely informational -- not needed for
//   extraction and not validated by the scanner:
//     u32  total_decompressed_size; // sum over all entries
//     u32  total_compressed_size;   // sum over all entries
//     u32  max_decompressed_size;   // max over all entries
//     u32  max_compressed_size;     // max over entries that are actually
//                                    // gzip-compressed (0 if every entry in
//                                    // the file is stored uncompressed)
//     u32  unknown;                 // always 3 in every sample
//     u32  pad[3];                  // always 0
//   Entry table (entry_count records), each:
//     char name[32];        // NUL-padded ASCII file name (no path)
//     u32  index_type;      // (entry_index << 16) | type; type 0 = stored
//                            // (raw bytes, compressed_size == decompressed
//                            // size), type 1 = gzip-compressed member (full
//                            // standard gzip stream: 10-byte header, raw
//                            // deflate, CRC32 + ISIZE trailer)
//     u32  decompressed_size;
//     u32  compressed_size; // on-disk payload length, whichever type
//     u32  offset;          // absolute offset of the payload in the file
//     u32  reserved;        // always 0
//   Payload data for every entry follows the entry table, laid out in
//   ascending offset order (not necessarily the same order as the table).
//
// Extract-only: no repacker beyond the synthetic test-fixture encoder in
// tests/mk_blpkg.py.
#ifndef SZS_LIB_BLPKG_H
#define SZS_LIB_BLPKG_H 1

#include "lib-nintendo.h"

bool IsBLPKG (const u8 *data, uint size);

// Scan D (size big) into a malloc-owned nintendo_sarc_entry_t list (name and
// payload both owned copies -- gzip members are decompressed into the
// owned buffer). Free the result with ResetOwnedEntries().
enumError ScanBLPKG (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

#endif // SZS_LIB_BLPKG_H
