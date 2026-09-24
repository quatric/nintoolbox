// SPDX-License-Identifier: GPL-2.0+
// "Aqua Panic!" (Wii, USA/En,Fr,Es disc) proprietary asset formats. No
// public documentation of any of these exists. Everything below was
// reverse-engineered from scratch against the retail disc, by extracting
// files/ and cross-checking multiple real samples of each extension
// byte-for-byte (never a single-sample guess).
//
// This disc has a full proprietary per-level scene/engine format family:
// nearly every level ships a matching .rck/.vis/.sts/.mb2/.mat/.lit/.ins/
// .gel/.col set (~100 files each). Four formats are covered here, at
// varying depth. Measured recognition/decode rate across every real
// sample of each extension extracted from the retail disc: .rck+.spa
// (105/105, outer "RKET" header only), .mat (100/100, 100% full chunk
// table), .mb2 (100/100, 100% full name-string table), .vis (100/100,
// 100%, fully decoded fixed record), .lit (100/100, 100%, fully decoded
// fixed record with one confirmed short-form/empty variant).
//
// Several other extensions on this disc are NOT covered here:
//   .sts, .ins, .gel, .col -- all four are magic-less per-level binary
//              blobs. .sts and .ins have a leading small integer that
//              looks like a record count, but (file_size - header) is
//              not evenly divisible by that count in any sample checked,
//              so no fixed record stride could be confirmed. .gel is a
//              constant 7128 bytes in 99 of 100 samples (one outlier at
//              2680 bytes) with a constant leading field, but no internal
//              record boundary could be confirmed either. .col is 52
//              bytes in 99 of 100 samples (28 in one outlier) and looks
//              like small counts + 16-bit fields with 0xffff sentinels,
//              but no consistent record shape held across both sizes.
//              Same policy as Mercury Meltdown Revolution's .mat/.nav
//              and Octomania's .scr -- left entirely unimplemented
//              rather than guessed at.
//   .ima    -- raw-looking headerless audio data (content resembles IMA
//              ADPCM sample bytes). This codebase has no notion of a
//              generic "headerless raw audio" file format elsewhere, so
//              this is left as extension-only recognition (no new format
//              registered).
//   .arc, .wad, .thp, .tpl, .wav, .bin, .img, .dol -- standard, already
//              generically supported Wii/GC formats, no changes needed.
//   .csv, .txt, .fre/.eng/.ita/.ger/.dut -- plain text, not binary
//              formats, no changes needed.
//   .scc    -- Microsoft Visual SourceSafe status file (`vssver.scc`), a
//              leftover development artifact, not a game asset format.
//
// (1) ".rck" / ".spa" -- shared "RKET" resource-graph/scene container.
//     Used both for per-level scene data (.rck) and localized language/UI
//     packs (.spa) -- identical outer header in both. Confirmed header:
//       char magic[4];    // "RKET" (fixed, all 105 samples)
//       u32  zero;        // Always 0 in every sample seen.
//       u32  hash;        // Per-file value, looks like a content hash
//                          // or checksum; not otherwise understood.
//       u16  version;     // Small integer (0x0001 in every sample seen).
//       u16  flags;       // Varies (0x0000 on .rck, 0xfd7f/0x1400/0x0000
//                          // seen on .spa) -- not decoded further.
//       u32  zero2;       // Always 0 in every sample seen.
//       u32  size_field;  // Does not equal file_size in any sample
//                          // checked (consistently smaller by a few
//                          // hundred to ~1000 bytes) -- looks like the
//                          // size of a leading data section excluding a
//                          // trailing table/footer, but the exact
//                          // relationship was not pinned down.
//     What follows the header (the actual resource-graph/scene-node
//     structure) was NOT reverse-engineered in this pass -- only the
//     fixed outer header above is decoded.
//
// (2) ".mat" -- "MATF" material table. Confirmed structure:
//       char magic[4];   // "MATF" (fixed, all 100 samples)
//       u32  field_a;    // LE. Always 8 in every sample seen.
//       u32  count_a;    // LE. Small integer, varies per file.
//       u32  count_b;    // LE. Small integer, varies per file (close to
//                         // but not always equal to count_a).
//     Immediately followed by a flat chunk table that runs to EOF in
//     every one of 100 real samples checked (including a random sample
//     cross-checked byte-for-byte):
//       char tag[4];     // e.g. "MAT " or "TEX " (space-padded, 4 bytes)
//       u32  payload_size; // LE, size of the following payload only
//       u8   payload[payload_size];
//     Two chunk kinds were seen: "MAT " (always 72-byte payload, a
//     fixed-size material property record -- individual field meaning
//     not decoded beyond the record boundary itself) and "TEX " (larger,
//     variable-size payload starting with what look like BE width/height
//     fields followed by packed texture-coordinate-like data -- payload
//     contents not reverse-engineered beyond the chunk boundary). This
//     module walks and reports the chunk table (tag + size for every
//     chunk); per-chunk payload semantics are not decoded.
//
// (3) ".mb2" -- "BNAM" bone/node name-string table. Confirmed structure:
//       char magic[4];    // "BNAM" (fixed, all 100 samples)
//       u32  body_size;   // LE. file_size - 8, confirmed in every sample.
//       u32  count;       // LE. Number of following length-prefixed
//                          // name strings, plus 1 (see pad byte below).
//       u32  flag;        // LE. Always 1 in every sample seen.
//       u8   pad;         // Always 0x00 in every sample seen -- appears
//                          // to be counted as an empty leading "entry"
//                          // in `count` above.
//     Followed by (count - 1) length-prefixed ASCII name strings, each:
//       u32  len;         // LE, includes the trailing NUL.
//       char name[len];   // NUL-terminated bone/mesh/node name (e.g.
//                          // "Vecteur", "Jaw", "medal_bronze"); a small
//                          // number of names carry a leading non-ASCII
//                          // marker byte inside the string data itself
//                          // (0x23 '#', 0xa4, 0x7e '~') whose meaning is
//                          // not understood.
//     Confirmed to consume the file exactly to EOF in every sample
//     checked (varying from 1 to 23 names per file).
//
// (4) ".vis" -- fixed-size visibility/flag record. Every one of 100 real
//     samples is exactly 24 bytes and byte-for-byte identical:
//       u32 field_0; // 1
//       u32 field_1; // 1
//       u32 field_2; // 0
//       u32 field_3; // 1
//       u32 field_4; // 0
//       u32 field_5; // 0
//     Fully decoded (fixed record, no variation observed across any
//     sample on the disc).
//
// (5) ".lit" -- single-light record. 99 of 100 real samples are exactly
//     48 bytes with a confirmed layout:
//       u32   date_tag;   // 0x20031126 in every sample -- looks like an
//                          // authoring-tool build date (2003-11-26).
//       u32   count;      // 1 in every 48-byte sample seen.
//       u32   zero;       // 0 in every sample seen.
//       u32   one;        // 1 in every sample seen.
//       float color_r;    // IEEE-754 float32, light color/intensity.
//       float color_g;
//       float color_b;
//       float pos_x;      // IEEE-754 float32, position-like value.
//       float pos_y;
//       float pos_z;
//       u32   zero2;      // 0 in every sample seen.
//       u32   zero3;      // 0 in every sample seen.
//     One real sample (`LangLvl.lit`) is a short-form 12-byte variant:
//     just date_tag + a zero count + a zero pad word, with no light
//     record following (confirmed valid empty state, count == 0). Both
//     shapes are fully decoded by this module.

#ifndef SZS_LIB_AQUAPANIC_H
#define SZS_LIB_AQUAPANIC_H 1

#include "types.h"
#include <stdio.h>

//-----------------------------------------------------------------------------
// (1) ".rck" / ".spa" "RKET" resource container (outer header only)

int IsAquaPanicRket (const u8 *data, size_t size, size_t file_size);
enumError DecodeAquaPanicRket_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (2) ".mat" "MATF" material chunk table

int IsAquaPanicMat (const u8 *data, size_t size, size_t file_size);
enumError DecodeAquaPanicMat_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (3) ".mb2" "BNAM" name-string table

int IsAquaPanicMb2 (const u8 *data, size_t size, size_t file_size);
enumError DecodeAquaPanicMb2_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (4) ".vis" fixed visibility/flag record

int IsAquaPanicVis (const u8 *data, size_t size, size_t file_size);
enumError DecodeAquaPanicVis_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (5) ".lit" single-light record

int IsAquaPanicLit (const u8 *data, size_t size, size_t file_size);
enumError DecodeAquaPanicLit_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // SZS_LIB_AQUAPANIC_H
