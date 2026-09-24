// SPDX-License-Identifier: GPL-2.0+
// "I Spy Spooky Mansion" (Wii, USA disc) proprietary asset formats. No
// public documentation of any of these exists (checked XeNTaX, GBAtemp,
// Models-Resource, romhacking.net -- all empty on this title). Everything
// below was reverse-engineered from scratch against the retail disc, by
// extracting files/ and cross-checking every real sample of each
// extension byte-for-byte (never a single-sample guess).
//
// A fourth, magic-less format is also covered:
//   .ges -- Wiimote gesture recording (28 real samples, all under
//           environment/*/minigames/*/gestures/*.ges). Every single one
//           of the 28 real samples is byte-for-byte the same shape (all
//           136 bytes): a 16-byte header of four BE u32 fields --
//           point_count (always 10), dim_x/dim_y/dim_z (always 1/1/1) --
//           followed by exactly point_count*3 (=30) BE float32 values,
//           i.e. one (x,y,z) triple per recorded point. See note (4).
//
// Three magic-based formats are covered:
//   .eid -- sound-event/effect table (161 real samples, all under
//           DATA/files/**). Magic "EID\0" header, outer effect table and
//           the per-effect "VARS" field-record table are both fully
//           confirmed and decoded byte-for-byte identically across 160 of
//           the 161 samples; see note (1).
//   .ast -- "SDASSETF" asset-bundle container (190 real samples, wildly
//           varying size and content: lights, materials, cursor models,
//           skeletal animations, textures). Outer header and the first
//           top-level chunk's tag/version/size are confirmed and decoded
//           for every sample; for a container that holds only small flat
//           sibling chunks (confirmed against all real *.ast samples
//           under DATA/files/Proteus/Lights.ast, the only case where the
//           whole file is made of nothing but such chunks) every chunk is
//           walked to EOF. For the (much more common) case of a single
//           top-level chunk that itself nests further sub-chunks (models,
//           animations, textures -- ALST/BMAP/etc.), only that first
//           chunk's header is decoded; its nested payload is NOT
//           reverse-engineered. See note (2).
//   .sdf -- "SDF\0" asset-type registry table (1 real sample,
//           DATA/files/Proteus/Wii.sdf). Outer header and the repeated
//           "TYPE"+4-char-type-tag+length records are confirmed; the
//           embedded per-type record tables reuse the exact same VARS
//           field-record layout as .eid, decoded with the same shared
//           helper. See note (3).
//
// (1) ".eid" -- confirmed against all 161 real samples (160 of them
//     byte-for-byte identical in shape; see below for the one exception):
//       char magic[4];   // "EID\0"
//       u32  version;    // BE, always 1
//       u32  reserved0;  // BE, always 0
//       u32  reserved1;  // BE, always 0
//       u32  effect_count; // BE, number of effect-table entries that follow
//     Followed by `effect_count` 20-byte effect-table entries:
//       char tag_eff[4]; // "EFF\0"
//       u32  string_table_ofs; // BE, absolute file offset of this
//                          // effect's VARS field-name string table
//       char tag_ogg[4]; // "OGG\0" (fixed; unrelated to the OGGFILE
//                          // field below, just a second sub-tag)
//       u32  vars_ofs;   // BE, absolute file offset of this effect's
//                          // "VARS" field-record table
//       u32  reserved;   // BE, always 0
//     At vars_ofs:
//       char tag_vars[4]; // "VARS"
//       u32  field_count; // BE, number of 16-byte field records that follow
//       u32  reserved0;   // BE, always 0
//       u32  reserved1;   // BE, always 0
//     Followed by `field_count` 16-byte field records:
//       u32  name_ofs; // BE, absolute file offset of this field's own
//                        // NUL-terminated ASCII name (confirmed
//                        // in-bounds and NUL-terminated in every field
//                        // of every one of the 161 samples)
//       u32  type;     // BE, one of 0/4/0x10/0x24 seen across every
//                        // sample; not otherwise decoded (looks like a
//                        // small value/string-pointer type tag, but its
//                        // exact enumeration was not pinned down)
//       char tag[4];   // BE, a short (<=4 char) mnemonic for the field,
//                        // e.g. "VOL\0"/"MAXD"/"STRM"/"FREQ" -- often
//                        // truncated or all-zero, purely cosmetic
//       u32  value;    // BE, either a raw IEEE-754 float (volume, min/max
//                        // distance, frequency), a plain integer (0/1
//                        // flags, channel/speaker index), or -- for the
//                        // OGGFILE field specifically -- an absolute file
//                        // offset of a second, longer NUL-terminated
//                        // string elsewhere in the same string table
//                        // (the referenced .ogg's relative path).
//     Right after the last field record comes this effect's raw
//     NUL-terminated name/field-name string table (name_ofs of every
//     field record of this effect, plus the OGGFILE field's value,
//     resolves cleanly into this region in every one of the 161 samples).
//     Exactly 17 distinct field names occur across every sample: VOL,
//     VOLCHAN, VOLSCALE, MINDIST, MAXDIST, OGGFILE, STREAMED, PRECACHE,
//     FREQUENCY, SPEAKER, AMBGROUP, PRECACHECOM, CHAINNEXT,
//     LOOPSTARTFILE, LOOPENDFILE, OGGLENGTHMS, LPFCUTOFF.
//     One exception found among the 161 real samples:
//       DATA/files/environment/solarium/riddle/payoff/riddle_02/payoff.eid
//     is byte-order-swapped (little-endian instead of big-endian -- e.g.
//     its magic reads as raw bytes 00 44 49 45 instead of 45 49 44 00).
//     This looks like a one-off export mistake by the original
//     developers rather than a second real variant, so it is left
//     unrecognized by the magic probe below rather than special-cased.
//
// (2) ".ast" -- "SDASSETF" asset-bundle container. Confirmed against all
//     190 real samples pulled from the disc (sizes from 240 bytes to
//     over 4.6MB): the file always opens with an 8-byte magic that reads
//     either as literal ASCII "SDASSETF" (151 of the 190 samples -- the
//     larger, in-game runtime assets: models, textures, animations) or,
//     word-swapped, i.e. each of the two 4-byte halves byte-reversed (39
//     of the 190 samples -- small utility assets exported from the
//     "Proteus" authoring tool: lights, materials, cursor models). Both
//     variants share one layout once byte order is normalized per file:
//       char magic[8];    // "SDASSETF" (or word-swapped)
//       u32  version;     // 1 or 2 in every sample seen
//       u32  chunk_count; // top-level chunk count
//     Followed by 16-byte-aligned chunk records, each with a 16-byte
//     header:
//       char tag[4];      // short ASCII chunk tag, e.g. "LGHT", "MTRL",
//                          // "ALST", "BMAP", "SKEL", "DATA"
//       u32  version;     // per-chunk version (0 or 1 for simple flat
//                          // chunks like "LGHT"; an unexplained
//                          // non-version-looking value for "MTRL")
//       u32  size;        // payload byte size immediately following
//                          // this 16-byte header
//       u32  field4;      // always 0 for the simple flat "LGHT"/"ALST"
//                          // top-level chunks; a small non-zero value
//                          // (e.g. name length or a flags/index field)
//                          // for "MTRL" sub-resource chunks -- its exact
//                          // meaning was not pinned down, so it is
//                          // decoded but not otherwise interpreted
//     followed by `size` bytes of payload, then padding up to the next
//     16-byte-aligned file offset before the next chunk header.
//     Confirmed end-to-end (chunk_count chunks decoded correctly all the
//     way to EOF) for every sample whose top-level chunks are all flat
//     siblings of this same 16-byte-header shape (e.g.
//     DATA/files/Proteus/Lights.ast: two back-to-back "LGHT" chunks).
//     For the much more common case -- a single top-level chunk (e.g.
//     "ALST" for a model/animation bundle, "BMAP" for a texture bundle)
//     whose own `size`-byte payload nests further sub-chunks of its own
//     -- only that first, outermost chunk's tag/version/size is decoded;
//     the nested sub-chunk layout inside its payload was NOT
//     reverse-engineered, so this module does not descend into it.
//
// (3) ".sdf" -- "SDF\0" asset-type registry table. Only one real sample
//     exists (DATA/files/Proteus/Wii.sdf, 816 bytes), so only what is
//     directly visible in that one file is decoded:
//       char magic[4];   // "SDF\0"
//       u32  version;    // BE, 1 in the one sample seen
//       u32  reserved;   // BE, 0 in the one sample seen
//       u32  type_count; // BE, number of TYPE records that follow
//     Followed by `type_count` 12-byte TYPE records:
//       char tag[4];       // "TYPE" (fixed)
//       char type_tag[4];  // 4-char (NUL-padded) asset-type tag, e.g.
//                            // "MCS\0", "OGG\0"
//       u32  length;        // BE, byte length of that type's embedded
//                            // record table that follows
//     In the one sample seen, both TYPE records' embedded tables are, in
//     turn, exactly the same "VARS"-tagged field-record layout used by
//     .eid (see note (1)), decoded here with the same shared helper.
//
// (4) ".ges" -- Wiimote gesture recording. Magic-less; identified purely
//     by exact size/shape (136 bytes: 16-byte header + 120 bytes = 30 BE
//     float32 values), confirmed identical across all 28 real samples.
//     The point_count/dim_x/dim_y/dim_z header fields are always 10/1/1/1
//     in every sample, so the probe below requires those exact values
//     rather than a fully general point_count*dims formula -- a
//     deliberately conservative choice given that no sample with
//     different dimensions was ever observed.

#ifndef SZS_LIB_ISPYSPOOKYMANSION_H
#define SZS_LIB_ISPYSPOOKYMANSION_H 1

#include "types.h"
#include <stdio.h>

//-----------------------------------------------------------------------------
// (1) ".eid" sound-event/effect table

int IsSpookyEid (const u8 *data, size_t size, size_t file_size);
enumError DecodeSpookyEid_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (2) ".ast" "SDASSETF" asset-bundle container

int IsSpookyAst (const u8 *data, size_t size, size_t file_size);
enumError DecodeSpookyAst_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (3) ".sdf" asset-type registry table

int IsSpookySdf (const u8 *data, size_t size, size_t file_size);
enumError DecodeSpookySdf_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (4) ".ges" Wiimote gesture recording

int IsSpookyGes (const u8 *data, size_t size, size_t file_size);
enumError DecodeSpookyGes_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // SZS_LIB_ISPYSPOOKYMANSION_H
