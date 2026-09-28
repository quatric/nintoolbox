// SPDX-License-Identifier: GPL-2.0+
// "Destroy All Humans! Big Willy Unleashed" (Wii, USA/En,Fr disc, RDHE78)
// proprietary asset formats. No public documentation of any of these exists
// (checked XeNTaX, GBAtemp, Models-Resource, romhacking.net -- all empty on
// this title). Everything below was reverse-engineered from scratch against
// the retail disc, by extracting DATA/files/ and cross-checking every real
// sample of each extension byte-for-byte (never a single-sample guess).
//
// Three formats are covered:
//   .stream  93/93  samples: outer envelope 100% confirmed (fixed-shape
//            header + zlib payload); the decompressed payload is the
//            engine's own generic "Chunk" object-serialization format
//            (confirmed by the "Chunk.cpp"/"ChunkManager.cpp" debug symbols
//            still present in dah_rls.elf) and is NOT reverse-engineered
//            beyond that -- see note (1).
//   .tbl    370/370 samples: fully reverse-engineered, 100% (a flat
//            UTF-16BE localized string table) -- see note (2).
//   .cnv    300/300 samples: fully reverse-engineered, 100% (a nested
//            conversation/speaker/line table) -- see note (3).
//
// Several other extensions on this disc are NOT covered here because they
// were confirmed to already be standard, generically-supported formats,
// not proprietary to this game:
//   .arc   -- standard Nintendo "U8-" archive (magic 0x55AA382D), confirmed
//             by extracting every sample with wszst; contains further
//             standard Wii home-menu formats (.tpl, .brlyt, .brlan, .brfnt).
//   .tpl   -- standard Nintendo TPL texture (magic 0x0020AF30), confirmed
//             by decoding samples with wimgt.
//   .bik   -- standard Bink video (RAD Game Tools), decodable with any
//             Bink-capable player/library.
//   .tga / .bmp -- plain, unmodified TGA/BMP images.
//
// Not yet inspected in this pass: ".AUD" (proprietary audio under
// AUDIO/BGM and AUDIO/VBK -- opens with small BE-looking header fields
// followed by data that does not match any standard PCM/ADPCM container
// checked so far; left for a future pass rather than guessed at).
//
// (1) ".stream" -- proprietary asset/level/UI resource stream. Confirmed
//     fixed-shape outer envelope, all big-endian, held across every one of
//     the 93 real samples on the disc:
//       u32  tag;          // varies per file (seen values include small
//                            // integers like 879 and 6256 as well as
//                            // larger ones like 31767074) -- looks like a
//                            // per-asset resource ID or content hash, not
//                            // a fixed magic. NOT reverse-engineered
//                            // beyond that.
//       u32  four;         // confirmed == 4 in every one of the 93
//                            // samples seen -- purpose not understood
//                            // (possibly a format/version tag), but its
//                            // fixed value makes it useful as part of a
//                            // content signature.
//       char hash[32];     // ASCII lowercase-hex digits (confirmed in
//                            // every sample) -- shape of an MD5 digest;
//                            // most likely a content hash of the
//                            // decompressed payload, used by the game's
//                            // asset cache to detect stale/rebuilt
//                            // streams. Not verified against the actual
//                            // MD5 of the payload.
//       u32  comp_size;    // size in bytes of the zlib stream that
//                            // follows; confirmed to exactly bound a
//                            // valid zlib stream in every sample.
//       u8   zlib[comp_size]; // raw zlib stream (RFC 1950, seen starting
//                            // "78 DA" -- best-compression preset in
//                            // every sample), inflates cleanly with
//                            // plain zlib in all 93 samples.
//     The decompressed payload is a chunk-tag-based object stream built by
//     the game's own generic serialization system (dah_rls.elf ships
//     "Chunk.cpp"/"ChunkManager.cpp" debug strings). Samples pulled from
//     this disc show it mixing raw compiled/embedded Lua source (a "FLUA"
//     tag immediately preceding readable Lua function bodies such as
//     "function GBL_SetUpLevel(...)") with further named sub-blocks (e.g.
//     "TEXTUREDICT", "RSF") whose tag table, per-tag header shape and
//     length-prefixing were NOT reverse-engineered in this pass -- this
//     module only decodes the outer envelope above and exposes the
//     decompressed payload (DecompressDahbwuStream()); walking the inner
//     chunk tags is future work.
//
// (2) ".tbl" -- localized UI/HUD string table (LANG/<lang>/*.tbl).
//     Confirmed structure, held across all 370 real samples (364
//     non-empty + 6 confirmed-valid empty 4-byte files with count==0):
//       u32 count;                    // number of entries that follow
//       repeat count times:
//         u32  key_len;
//         char key[key_len];          // ASCII identifier, e.g. "HUD_ZAP"
//         u32  str_len;               // length in UTF-16 code units
//         u16  str[str_len];          // UTF-16BE display string
//     All fields big-endian. The `count` field was confirmed to exactly
//     account for every entry through to EOF in all 370 samples.
//
// (3) ".cnv" -- conversation/speaker/line-ID table (CONV/<lang>/*.cnv).
//     Confirmed structure, held across all 300 real samples:
//       u32 conv_count;                     // number of conversations
//       repeat conv_count times:
//         u32  name_len;
//         char name[name_len];              // ASCII, e.g. "M16_IGCIntro"
//         u32  line_count;
//         repeat line_count times:
//           u32  speaker_len;
//           char speaker[speaker_len];      // ASCII, e.g. "SPEAKER_CRYPTO"
//           u32  line_id_len;
//           char line_id[line_id_len];      // ASCII, e.g.
//                                            // "CR_M16_IGCINTRO_1" -- the
//                                            // matching ".AUD" base name.
//     All fields big-endian, no NUL terminators. `conv_count`/`line_count`
//     were confirmed to exactly account for every record through to EOF
//     in all 300 samples (verified by a byte-for-byte structural walk of
//     the entire disc's .cnv set, not spot-checked).

#ifndef SZS_LIB_DAHBWU_H
#define SZS_LIB_DAHBWU_H 1

#include "types.h"
#include <stdio.h>

//-----------------------------------------------------------------------------
// (1) ".stream" asset/level/UI resource stream

int IsDahbwuStream (const u8 *data, size_t size, size_t file_size);
enumError DecodeDahbwuStream_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

// Inflate a ".stream" file's zlib payload into a freshly malloc()'d buffer.
// On success, returns ERR_OK, sets *out_data (caller must FREE() it) and
// *out_size to the decompressed length. The outer envelope (tag/hash/
// comp_size) is not included -- only the decompressed chunk payload.
enumError DecompressDahbwuStream (
	const u8 *data, size_t size, u8 **out_data, size_t *out_size);

//-----------------------------------------------------------------------------
// (2) ".tbl" localized UTF-16BE string table

int IsDahbwuTbl (const u8 *data, size_t size, size_t file_size);
enumError DecodeDahbwuTbl_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (3) ".cnv" conversation/speaker/line-ID table

int IsDahbwuCnv (const u8 *data, size_t size, size_t file_size);
enumError DecodeDahbwuCnv_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // SZS_LIB_DAHBWU_H
