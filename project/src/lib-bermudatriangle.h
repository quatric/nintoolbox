// SPDX-License-Identifier: GPL-2.0+
// "Bermuda Triangle - Saving the Coral" (Wii, USA disc) proprietary asset
// formats. No public documentation of any of these exists. Everything
// below was reverse-engineered from scratch against the retail disc, by
// extracting files/ and cross-checking every real sample of each
// extension byte-for-byte.
//
// Formats covered:
//   .MWT (outer wrapper only) -- "GDATAVERSION" resource envelope. Most
//         .MWT files on this disc open with this 0x40-byte header; the
//         payload that follows it is, for every sample checked, an
//         already-supported format: a Camelot-style GX texture bank
//         (magic 0x0020af30, big-endian -- see lib-camtexbank.c/.h,
//         originally reverse-engineered for Mario Golf/Power Tennis but
//         structurally identical here down to the format-code table and
//         entry layout). A minority of .MWT files (e.g. RECTMARK.MWT,
//         LICENSE.MWT) skip the GDATAVERSION envelope and are the bare
//         Camelot bank by itself -- those need no new code at all, they
//         are already handled by the existing Camelot detector/decoder.
//   .MWG / .MSP -- "PLANETG" generic tagged-object serialization (the
//         engine backing this game appears to be called "Planet-G",
//         from the root tag strings seen in every sample). Both
//         extensions share byte-for-byte the same recursive
//         length-prefixed string + field format; see note (1).
//   .PKI -- "IMAGE_WII_COMPACT_FILE_VERSION_1" texture-pack container:
//         a flat header + name/size/offset entry table bundling many
//         .MWT payloads into one archive; see note (2).
//   .pgf -- bitmap/vector font resource. Header (name + point size) is
//         confirmed; the table that follows looks like an ascending
//         per-glyph offset table but was not conclusively decoded (only
//         4 samples, no glyph count field found to cross-check against);
//         see note (3). Left as header-only detection/decode.
//
// Extensions confirmed already standard and NOT covered here:
//   .dsp   -- standard Nintendo DSP-ADPCM audio.
//   .tga   -- standard Targa image (source art, not shipped to the GX
//             texture pipeline directly).
//   .wad   -- standard Wii title WAD.
//   .arc   -- standard Nintendo U8 archive.
//   .tpl   -- standard Nintendo TPL texture palette.
//   .dol   -- standard Dolphin executable.
//   .bnr   -- standard Wii banner.
//   .jpg / .gif -- standard image formats (loading-screen art).
//   .txt / .csv -- plain text / localization tables.
//   .bin / .img / .sh / .bat -- standard disc metadata and PC/dev-side
//             build scripts, not game-proprietary.
//
// Extension confirmed as NOT a game asset:
//   .db -- Thumbs.db (OLE2/CFBF compound file, magic d0 cf 11 e0), a
//          Windows Explorer thumbnail cache leftover from development;
//          skipped entirely, no support added.
//
// (1) ".MWG" / ".MSP" -- "PLANETG" tagged-object format. Confirmed
//     against GameSytem.MWG (2708 bytes), Continue.MWG, Title.MWG and
//     LOAD.MSP (153174 bytes): the file is a sequence of nodes, each
//     node being a u32 length followed by that many bytes of ASCII (not
//     NUL-terminated) -- the root node's string is always
//     "PLANETG_SPRITECOLLECTION" (.MWG) or "PLANETG_SPRITE" (.MSP),
//     confirming this is the same underlying serializer for both
//     extensions. Every ASCII tag name seen (PLANETG_SPRITECOLLECTION,
//     BUTTON_N, BTN, OBJ, LOAD, LOADING, CROWN1, CROWN2, WATER1, ...) is
//     unambiguous and was walked and printed successfully in every real
//     sample. What is NOT conclusively pinned down is the exact meaning
//     of the u32 fields interleaved between tag strings (child counts vs.
//     numeric IDs vs. plain data values all appear, and which is which
//     was not solved with only two real files to cross-check) -- so the
//     decoder below does a byte-honest heuristic walk of the file,
//     printing every length-prefixed ASCII run it finds as a tag/string
//     and every other u32 as a raw numeric field, without claiming to
//     reconstruct the exact tree shape or field semantics.
//
// (2) ".PKI" -- "IMAGE_WII_COMPACT_FILE_VERSION_1" texture-pack
//     container. Confirmed against the one real sample, CoralPack.PKI
//     (3889632 bytes):
//       char magic[32];   // "IMAGE_WII_COMPACT_FILE_VERSION_1", fixed,
//                         // NOT NUL-terminated/padded.
//       u32  entry_count; // LE.
//     Followed immediately by `entry_count` variable-length entries:
//       u32  name_len;    // LE, length of the following path string.
//       char name[name_len]; // Windows-style backslash path, e.g.
//                         // "DATA\CORALDATA\PLAYGAME\CORAL\03\01.MWT",
//                         // NOT NUL-terminated.
//       u32  data_size;   // LE. Matches a plausible .MWT payload size in
//                         // every entry checked.
//       u32  data_offset; // LE. Strictly increasing across entries in
//                         // the sample checked; consistent with an
//                         // offset into a trailing data section (not
//                         // located/verified against real payload bytes
//                         // -- the entry table itself is what is
//                         // confirmed here, not the data section split).
//     This directly mirrors the "DATA\...\NN.MWT" naming visible in the
//     loose .MWT tree, i.e. this pack simply bundles many of that
//     game's individual .MWT files (see note above) under one name/size/
//     offset table.
//
// (3) ".pgf" -- font resource. Confirmed header, all 4 real samples
//     (consolas20.pgf, Tahoma16.pgf, tahoma20.pgf, comic.pgf):
//       u32  name_len;    // LE.
//       char name[name_len]; // font family name, e.g. "Consolas",
//                         // "Tahoma", "Comic Sans MS" -- NOT
//                         // NUL-terminated.
//       u32  point_size;  // LE. 0x14=20 for consolas20.pgf/tahoma20.pgf,
//                         // 0x10=16 for Tahoma16.pgf -- matches the
//                         // point size baked into each filename exactly
//                         // (comic.pgf, no size in its filename, has
//                         // 0x14=20 here too).
//     Followed by a run of ascending u32 values that looks like a
//     per-glyph offset table (into a glyph atlas or outline blob later
//     in the file), but no confirmed glyph-count field or terminator was
//     found to pin down the table's length or the payload that follows
//     it, so only the header above is decoded.

#ifndef SZS_LIB_BERMUDATRIANGLE_H
#define SZS_LIB_BERMUDATRIANGLE_H 1

#include "types.h"
#include <stdio.h>

//-----------------------------------------------------------------------------
// (0) ".MWT" "GDATAVERSION" outer resource envelope

int IsBermudaMwt (const u8 *data, size_t size, size_t file_size);
enumError DecodeBermudaMwt_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (1) ".MWG" / ".MSP" "PLANETG" tagged-object serialization

int IsBermudaPlanetG (const u8 *data, size_t size, size_t file_size);
enumError DecodeBermudaPlanetG_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (2) ".PKI" "IMAGE_WII_COMPACT_FILE_VERSION_1" texture-pack container

int IsBermudaPki (const u8 *data, size_t size, size_t file_size);
enumError DecodeBermudaPki_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (3) ".pgf" font resource

int IsBermudaPgf (const u8 *data, size_t size, size_t file_size);
enumError DecodeBermudaPgf_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // SZS_LIB_BERMUDATRIANGLE_H
