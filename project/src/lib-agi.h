// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Toys for Bob "AGI" archives (Skylanders: Swap Force and likely other
// Toys for Bob titles; every *.pak under files/ starts with this magic).
// All fields big-endian.
//
//   0x00 u32 magic            0x1A414749 ("\x1AAGI")
//   0x04 u32 version           (9 in all samples)
//   0x08 u32 entry_table_size  (byte length of the entry table at 0x40)
//   0x0c u32 name_count        (number of packed members / named entries)
//   0x10 u32 block_align       (0x800 in all samples; data offsets tend to
//                               be aligned to this)
//   0x14 u32 table_hash        (unclear purpose, ignored)
//   0x18 u32 unk1
//   0x1c..0x28 reserved / zero in samples
//   0x2c u32 name_table_offset (absolute file offset of the name table)
//   0x30 u32 name_table_size   (byte length of the name table)
//   0x34 u32 unk2              (3 in all samples)
//   0x38 u32 hash1
//   0x3c u32 hash2
//   -- header is 0x40 (64) bytes total --
//
//   name_table_offset + name_table_size == file size  (verified invariant,
//   used here as the format sanity check).
//
// Entry table (bytes [0x40 .. 0x40+entry_table_size)): per-entry records are
// NOT a fixed stride across every .pak file -- there are two distinct record
// layouts, and ScanAGI() tries both (see lib-agi.c for the implementation):
//
//  1. Pure-audio-bank .pak files (~2/3 of retail samples): entry records are
//     plain 2-word (offset, size) big-endian u32 pairs, both in bytes, with
//     size already the exact on-disk length. ScanAGI() finds these by
//     scanning for adjacent words with 0x40 <= offset < name_table_offset
//     and offset+size <= file size; the raw member is data[offset..+size].
//
//  2. Mixed audio + .igz/.igx model/material .pak files (~1/3 of retail
//     samples, e.g. glitteringtiara_hatdata.pak): entry records are 4-word
//     groups instead, in the same order as the name table:
//       word0: 0 (reserved; zero in every sample seen)
//       word1: block-aligned (0x800) absolute file offset of the member
//       word2: NOT a reliable on-disk size -- it is close to exact for
//              small/uncompressed members (e.g. an 8-byte "<igx/>\n\n" stub
//              really is 8 bytes) but does not match the on-disk length for
//              members whose payload is opaque/high-entropy (mostly .igz),
//              so it looks like a decompressed/logical size rather than the
//              stored size and is NOT used to bound extraction
//       word3: a per-entry flag, either 0xffffffff or 0x2000000x; meaning
//              beyond "small stub-like entry" vs. "regular entry" unclear
//     Since word2 can't be trusted, each member's on-disk byte range is
//     derived the same way the 2-word case trusts adjacent offsets: from
//     the *next* distinct block-aligned offset in ascending order (or
//     name_table_offset for the highest-offset member). Verified against
//     780 real mixed .pak files pulled from the Swap Force DATA partition:
//     every resulting byte range starts with the expected member signature
//     (0x5d00 at byte offset 2, the common "AGI object" blob header shared
//     by both .igz and most .igz-format .igx members, or an ASCII '<' for
//     the handful of plain-XML .igx stubs) 100% of the time.
//
// ScanAGI() tries layout 1 first (cheaper, and it's the majority case),
// falls back to layout 2, and requires the number of parsed entries to
// exactly equal name_count either way; if neither layout produces exactly
// name_count entries it fails cleanly (EINVAL) rather than emit wrong or
// truncated data, so genuinely unrecognized files are simply skipped.
// Across all ~2339 retail .pak files sampled from the Swap Force DATA
// partition, this combination (layout 1 + layout 2) accounts for 100% of
// them: 1559 via layout 1, 780 via layout 2, 0 unresolved.
//
// Name table (bytes [name_table_offset .. name_table_offset+name_table_size)):
//   [0 .. name_count*4): name_count big-endian u32 byte offsets, each
//     relative to the start of the name table, pointing at that entry's
//     name string.
//   At each such offset: a NUL-terminated backslash-path string (Windows
//     dev path, e.g. "U:\temporary\wii\sounds\...\EVENT_Cannon_Launch"),
//     immediately followed by 4 more bytes (a hash/CRC, purpose unknown,
//     skipped here) before the next string's bytes begin.
//
// Sub-formats are extracted raw, not decoded: audio bank members start with
// an FSB5 (FMOD Sound Bank 5) header -- this repo's existing FSB4/FSB5
// decoder (lib-fsb.c) can decode those once extracted. Model/material
// members use Toys for Bob's proprietary "IGZ" object-graph format
// (unrelated to any other "IGZ"-like format in this repo) and are simply
// extracted as raw .igz/.igx blobs; IGZ internals are out of scope here.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_AGI_H
#define SZS_LIB_AGI_H 1

#include "lib-nintendo.h"

enumError ScanAGI (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
