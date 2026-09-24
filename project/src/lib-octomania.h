// SPDX-License-Identifier: GPL-2.0+
// "Octomania" (Wii, USA disc) proprietary asset formats. No public
// documentation of any of these exists (checked XeNTaX, GBAtemp,
// Models-Resource, romhacking.net -- all empty on this title). Everything
// below was reverse-engineered from scratch against the retail disc, by
// extracting files/ and cross-checking every real sample of each
// extension byte-for-byte (never a single-sample guess).
//
// Two formats are covered:
//   .sec -- "secB" scene/demo container (5 real samples: utdemo.sec,
//           efdemo.sec, g3ddemo.sec, lytdemo.sec, snddemo.sec, all under
//           DATA/files/data/). Outer header and top-level chunk table
//           confirmed and decoded; see note (1).
//   .wt  -- wavetable/instrument sample-offset index (1 real sample:
//           DATA/files/midi/gm16adpcm.wt, paired with the raw sample data
//           in gm16adpcm.pcm in the same directory). Leading offset table
//           confirmed and decoded; see note (2).
//
// Extensions confirmed already standard and NOT covered here:
//   .dsp    -- standard Nintendo DSP-ADPCM audio (already handled).
//   .arc    -- standard Nintendo U8 archive (magic 0x55AA382D).
//   .thp    -- standard Nintendo THP video.
//   .wad    -- standard Wii title WAD.
//   .tpl    -- standard Nintendo TPL texture palette.
//   .brsar  -- standard Wii RSAR sound archive.
//   .dol    -- standard Dolphin executable.
//   .bnr    -- standard Wii banner.
//   .mid    -- standard MIDI ("MThd" header confirmed).
//   .csv/.txt -- plain text (UTF-16-ish localized string tables / notes).
//   .bin/.img -- standard disc metadata (tmd.bin/ticket.bin/cert.bin/
//               h3.bin/bi2.bin, apploader.img), not game-proprietary.
//   .pcm    -- raw sample data referenced by ".wt" (see note (2)); not a
//              distinct container format of its own, so no probe/decoder
//              of its own is registered for it.
//
// Extension inspected but NOT reverse-engineered:
//   .scr -- DATA/files/ban1.scr (43010 bytes, only sample seen). Opens
//           with a u16 0x0006 followed by a huge run of 0xffff bytes to
//           EOF. With only a single real sample available, no confirmed
//           structure (record table, string data, or anything besides the
//           leading u16 and the 0xffff filler) could be pinned down, so
//           this is left entirely unimplemented rather than guessed at
//           -- same policy as Mercury Meltdown Revolution's .mat/.nav.
//
// (1) ".sec" -- "secB" container. Confirmed against all 5 real samples,
//     cross-checking field values and offsets in every one:
//       char magic[4];    // "secB" (fixed, all 5 samples)
//       u32  version;     // BE. Always 1 in every sample seen.
//       u32  block_size;  // BE. Always 0x800 in every sample seen --
//                          // looks like a fixed alignment/block-size
//                          // constant for this container family.
//       u32  string_tab_ofs; // BE. Byte offset of a shared, shared-name
//                          // NUL-terminated ASCII string table that
//                          // follows the chunk table (confirmed: every
//                          // sample's bytes at this offset are printable
//                          // ASCII names such as "archiveFont",
//                          // "layout.arc", "sound_data.brsar").
//       u32  block_size2; // BE. Always 0x800 again in every sample seen
//                          // (same value as block_size).
//       u32  count;       // BE. Number of 32-byte chunk-table entries
//                          // that immediately follow the header (table
//                          // starts right at offset 0x20).
//       u32  group_count; // BE. Small integer, meaning not fully pinned
//                          // down (matches the entry-table's outer
//                          // group nesting depth in every sample: 1 or 2).
//       u32  leaf_count;  // BE. Matches chunk_table[0].leaf_count below
//                          // (confirmed identical in all 5 samples).
//     Followed immediately by `count` chunk-table entries of 32 bytes
//     each (8 big-endian u32 fields), confirmed structurally identical
//     in shape across all 5 samples:
//       - Entry 0 is always a "group" descriptor: its first field holds
//         the number of following leaf/name entries (confirmed equal to
//         the header's leaf_count field in every sample).
//       - Entries 1 .. count-2 are "name" entries: field[1] = a 0-based
//         index, field[2] = a small tag (1 in every sample seen),
//         field[3] = 1 + the byte offset of this entry's NUL-terminated
//         name within the shared string table at string_tab_ofs
//         (confirmed by resolving every name entry's field[3]-1 against
//         the string table in all 5 samples and getting a clean,
//         in-bounds NUL-terminated ASCII name every time), field[5] = a
//         secondary offset whose target region (inside the table area,
//         between the entry table and the string table) was NOT
//         reverse-engineered.
//       - The last entry (index count-1) is a "data" trailer: field[2]
//         is a distinct tag (0x100/256 in every sample seen), field[4]
//         repeats the leaf/group count, and field[7] holds a large value
//         that looks like an overall payload byte size for the chunk's
//         binary data blob that follows the table (plausible against
//         file size in every sample checked).
//     The actual per-chunk binary payload data (fonts, effects, 3D
//     geometry, layout, sound sub-resources -- as suggested by the
//     ut/ef/g3d/lyt/snd filename prefixes) was NOT reverse-engineered;
//     this module only decodes the outer header and the chunk table
//     (names + tags), not the sub-resource contents themselves.
//
// (2) ".wt" -- wavetable/instrument sample-offset index. Confirmed
//     against the one real sample pulled (DATA/files/midi/gm16adpcm.wt,
//     193082 bytes, paired with DATA/files/midi/gm16adpcm.pcm, 881485
//     bytes): the file opens with a table of big-endian u32 byte offsets
//     (24, 0x8018, 0x10018, 0x18c88, 0x27a58, 0x29948, ... -- all
//     confirmed to be valid, strictly increasing, in-bounds byte offsets
//     into the companion ".pcm" file, i.e. sample start offsets),
//     terminated by a run of 0xffffffff sentinel entries. What follows
//     later in the file -- a run of sequential small u16 values (looks
//     like a MIDI-program-to-sample index map) and further per-entry
//     metadata blocks near EOF -- was NOT reverse-engineered; this
//     module only decodes the leading sample-offset table.

#ifndef SZS_LIB_OCTOMANIA_H
#define SZS_LIB_OCTOMANIA_H 1

#include "types.h"
#include <stdio.h>

//-----------------------------------------------------------------------------
// (1) ".sec" "secB" scene/demo container

int IsOctomaniaSec (const u8 *data, size_t size, size_t file_size);
enumError DecodeOctomaniaSec_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (2) ".wt" wavetable sample-offset index

int IsOctomaniaWt (const u8 *data, size_t size, size_t file_size);
enumError DecodeOctomaniaWt_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // SZS_LIB_OCTOMANIA_H
