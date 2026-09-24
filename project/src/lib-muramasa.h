// SPDX-License-Identifier: GPL-2.0+
// "Muramasa - The Demon Blade" (Wii) proprietary asset formats. No public
// documentation of any of these exists (checked XeNTaX, GBAtemp,
// Models-Resource, romhacking.net -- all empty on this title). Everything
// below was reverse-engineered from scratch against the retail disc, by
// extracting files/ and cross-checking real samples of each extension.
//
// Three formats are covered:
//
// (1) A generic "FCMP" compressed container that wraps six of the on-disc
//     extensions (".mbs", ".ftx", ".esb", ".nsb", ".abf", ".nms"), each
//     holding a fixed, distinct typed inner sub-blob tag. Confirmed
//     outer header, checked against 307 real samples across all six
//     extensions pulled from the retail disc:
//       char magic[4];      // "FCMP" (fixed)
//       u32  decomp_size;   // LE. Decompressed payload size. Confirmed
//                             // >= (file_size - 13) in every one of the
//                             // 307 samples checked (i.e. never smaller
//                             // than the on-disc compressed payload),
//                             // consistent with a real compression ratio
//                             // (observed up to ~3.6x on this disc).
//       u32  reserved;      // LE. Observed exactly 0x12340000 in every
//                             // single sample checked (307/307) -- looks
//                             // like a fixed sentinel/version constant,
//                             // meaning not otherwise understood.
//       u8   flag;          // Observed values: 0xff, 0xef, 0x5f -- always
//                             // ends in the nibble 0xf, only the upper
//                             // nibble varies (0xf/0xe/0x5). Likely some
//                             // combination of a compression-method/level
//                             // selector, but the exact meaning was not
//                             // confirmed (see below).
//     followed immediately, with NO further framing, by the compressed
//     payload, whose first several bytes are confirmed to be a direct,
//     unencoded copy of the inner sub-blob's 4-byte magic (see the table
//     below) -- i.e. compression is either not applied to the start of
//     the stream, or (more likely, see next paragraph) this is a classic
//     LZ77-family scheme whose very first token(s) are necessarily
//     literal copies, since no back-reference history exists yet at the
//     start of any stream.
//
//     The inner sub-blob tag is a hard 1:1 mapping with the on-disc
//     extension, confirmed across every sample (no exceptions):
//       ".mbs" -> "FMBS" (642/642 samples)
//       ".ftx" -> "FTEX" (610/610 samples)
//       ".esb" -> "EMBP" (74/74 samples)
//       ".nsb" -> "NSBD" (60/60 samples)
//       ".abf" -> "MLIB" (57/57 samples)
//       ".nms" -> "NMSB" (10/10 samples)
//
//     COMPRESSION NOT CRACKED. What was tried: the project already ships
//     a Yaz0/Yaz1 LZ77 decompressor (fastyz.c/trueyz.c, wired via
//     DecompressYAZ() in lib-szs.h) used by several other Nintendo
//     formats, so that was the first thing checked -- ruled out
//     immediately since the outer magic is "FCMP", not "Yaz0"/"Yaz1", and
//     the header layout (13 bytes: magic + LE size + reserved + flag) does
//     not match Yaz0's (16 bytes: magic + BE size + 8 reserved bytes). A
//     from-scratch, standalone re-implementation of the classic Yaz0
//     bitstream (a per-8-token flag byte, MSB first, selecting between a
//     literal byte and a 2-3 byte back-reference token) was then written
//     and run against several real samples treating the payload right
//     after the 13-byte header as the raw Yaz0-style stream. The initial
//     literal run decoded correctly and reproduced the expected inner
//     magic bytes exactly (confirming the header/payload boundary is
//     right), but the very first back-reference token encountered after
//     that literal run always produced an out-of-range distance (larger
//     than the amount of output produced so far, in both MSB-first and
//     LSB-first flag-bit-order interpretations, and also after undoing
//     the flag-byte-vs-header-byte off-by-one that the first attempt hit).
//     This rules out the standard Yaz0/Yaz1 token encoding specifically,
//     without ruling out every possible LZ variant -- but pinning down a
//     bespoke, undocumented token/distance encoding purely from black-box
//     byte inspection (no executable/disassembly available in this
//     session) was judged to need real algorithmic reverse engineering
//     beyond what a black-box byte-pattern pass can responsibly confirm,
//     so per this project's policy of not asserting semantics that were
//     not actually verified, the compressed payload itself is NOT decoded
//     here. Only the outer FCMP header and the inner sub-blob's magic tag
//     are reported.
//
// (2) ".otb" -- "OTB " table. Confirmed outer header (only 2 real samples
//     exist on this disc -- both are the same underlying table, once
//     region-neutral and once under files/_US/, so the values below are
//     corroborating rather than independently confirming):
//       char magic[4];     // "OTB " (fixed, note trailing space)
//       u32  body_size;    // LE.
//       u32  header_size;  // LE. Confirmed body_size + header_size ==
//                            // file_size exactly in both samples (observed
//                            // value 0x260 / 608 in both).
//       u32  count;        // LE. Observed 0x7e (126) in both samples.
//     followed by a dense table (u16 offset-looking entries with many
//     0xffff "unused" slots, then what looks like a second, separate
//     table of BE-ish u32 values further in) that was NOT reverse-
//     engineered -- too few distinct samples (both essentially the same
//     table) to confidently pin down a per-entry record shape. Only the
//     header fields above are decoded/reported.
//
// (3) ".nsi" -- "NSI " sound info table. Confirmed header (1 sample on
//     this disc):
//       char magic[4];     // "NSI " (fixed, note trailing space)
//       u32  body_size;    // LE.
//       u32  tail_size;    // LE. Confirmed body_size + tail_size ==
//                            // file_size exactly.
//     followed by a sparse table of small LE u16/u32 values (mostly
//     zero-padded) that was NOT reverse-engineered -- only one real
//     sample exists on this disc, not enough to distinguish a per-entry
//     record shape from incidental zero padding. Only the header fields
//     above are decoded/reported.
#ifndef LIB_MURAMASA_H
#define LIB_MURAMASA_H 1

#include "lib-std.h"

//-----------------------------------------------------------------------------
// (1) "FCMP" compressed container wrapping ".mbs"/".ftx"/".esb"/".nsb"/
// ".abf"/".nms" -- outer header + inner sub-blob tag only, compressed
// payload NOT decoded (see above for what was tried).
int IsMuramasaFcmp (const u8 *data, size_t size, size_t file_size);
enumError DecodeMuramasaFcmp_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (2) ".otb" "OTB " table -- header only, entry table not decoded.
int IsMuramasaOtb (const u8 *data, size_t size, size_t file_size);
enumError DecodeMuramasaOtb_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (3) ".nsi" "NSI " sound info table -- header only, entry table not decoded.
int IsMuramasaNsi (const u8 *data, size_t size, size_t file_size);
enumError DecodeMuramasaNsi_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // LIB_MURAMASA_H
