// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Hudson Soft/Racjin "cddata*.dig" streaming resource package (Bomberman
// Land, Wii; files/cddata.dig, files/cddata1.dig .. files/cddata14.dig).
//
// No public documentation of this exact game's ".dig" layout was found
// (a related, non-identical "CDDATAx.DIG" layout is documented for the
// sibling Racjin title Bomberman Kart DX on ZenHAX, and a "CFC.DIG"
// sibling format for a Naruto title exists as a QuickBMS script -- neither
// matches this game's samples byte-for-byte). This implementation was
// reverse-engineered from real retail samples plus a decompiled trace of
// the disc-read dispatcher in main.dol (the function owning the
// "/cddataN.dig" path table), then re-verified against real bytes: every
// resolved (offset, size) range in the samples below was checked to
// actually contain one or more recognisable embedded Nintendo TPL texture
// headers (magic 00 20 AF 30) at the expected byte positions.
//
// Resources are addressed by a 32-bit numeric ID; the .dig container
// itself carries no filenames, so extracted members are named by table
// index only ("entry_%04u.bin"). Real asset names/IDs live in other game
// data (object/level tables) elsewhere on the disc, not in the .dig file.
//
// Confirmed layout, all fields big-endian:
//   Header (12 bytes):
//     u32 type;      // 1 or 6 in every sample seen; meaning of the
//                     // specific values not resolved, but see "type == 2"
//                     // note below (this repo only ever observed 1 or 6)
//     u32 unknown1;   // varies per file, not a byte/entry count; unresolved
//     u32 entry_hint; // roughly (but not always exactly) the number of
//                      // populated entries below; not relied on for
//                      // parsing, only used as a sanity cross-check
//   Entry table: fixed-size array of 16-byte big-endian records filling
//   exactly the first 0x800 (2048) bytes of the file (i.e. right after the
//   12-byte header, zero-padded out to the sector boundary):
//     u32 offset_sectors; // real byte offset = offset_sectors << 11
//     u32 size_sectors;   // real byte size   = size_sectors << 11
//     u32 unknown_a;      // small integer (single-digit to double-digit)
//                          // in every sample; purpose unresolved
//     u32 reserved;        // always 0 in every sample seen
//   All-zero records (offset_sectors == 0 && size_sectors == 0) are unused
//   slots and are skipped; populated records need not be contiguous within
//   the table. Each resolved (offset, size) range is a single opaque
//   "resource" blob -- in the confirmed samples these are themselves a
//   concatenation of several standalone sub-assets (e.g. multiple raw TPL
//   textures back-to-back with zero padding), which this extractor does
//   not attempt to split further; run the repo's ordinary file-type
//   detection over an extracted blob (or over the whole .dig again with a
//   generic archive scan) to pull out recognisable sub-assets.
//
// Unsupported "type == 2" composite variant: exactly 5 of the 15 real
// samples checked (the ~13.6-13.9 MB files: cddata2/5/8/11/14.dig) do not
// use the flat table above -- their first 0x800 bytes contain no record
// whose resolved range contains any recognisable embedded content, and
// three of the five (cddata2/11/14.dig) are byte-identical to each other
// except for one per-entry ID-like field, consistent with a shared,
// differently-addressed nested/composite table the decompiled trace
// describes as "type == 2" (sub-tables pointing at further, separately
// opened files) but does not fully resolve. ScanDIG() detects this case
// (zero valid entries found) and returns EINVAL for those files rather
// than guessing; they are not extracted.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_DIG_H
#define SZS_LIB_DIG_H 1

#include "lib-nintendo.h"

enumError ScanDIG (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

// Lightweight detection sniff: 'data'/'data_size' need only cover the first
// 0x800 bytes (the entry table); 'real_size' is the true, full file size
// used to bounds-check resolved entries without dereferencing past
// 'data_size'. Returns true iff at least one entry resolves to a plausible,
// in-bounds range (i.e. the flat table variant, not "type == 2").
bool IsDIG (const u8 *data, uint data_size, u64 real_size);

#endif
