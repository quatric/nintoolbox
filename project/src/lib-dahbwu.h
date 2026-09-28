// SPDX-License-Identifier: GPL-2.0+
// "Destroy All Humans! Big Willy Unleashed" (Wii, USA/En,Fr disc, RDHE78)
// proprietary asset formats. No public documentation of any of these exists
// (checked XeNTaX, GBAtemp, Models-Resource, romhacking.net -- all empty on
// this title). Everything below was reverse-engineered from scratch against
// the retail disc, by extracting DATA/files/ and cross-checking every real
// sample of each extension byte-for-byte (never a single-sample guess).
//
// Four formats are covered:
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
//   .AUD    19938/19938 samples: NOT a proprietary format at all --
//            19745/19938 (99.03%) are already the standard Nintendo
//            GC/Wii mono DSP-ADPCM stream this project detects generically
//            as FF_DSP (see lib-dsp.h), byte-for-byte, including its exact
//            0x60-byte header and nibble-count formula; the remaining
//            193/19938 (large streamed background music/ambient tracks)
//            have no header at all and are confirmed (by a waveform-
//            smoothness check against every one of the 193 real samples)
//            to be plain big-endian 16-bit PCM from byte 0 -- see note (4).
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
//     The decompressed payload is a named-resource stream built by the
//     engine's generic "Pcpl::AssetHandler" system (dah_rls.elf, not
//     stripped, still has the full symbol table: AssetHandler::
//     RegisterHandler()/HandleAssetChunk(), AssetManager::RegisterAsset(),
//     and one HandleAssetFn__<N>Handler symbol per resource type). Each
//     top-level resource record has this confirmed fixed-shape header,
//     cross-checked against 5 different real .stream samples and every
//     occurrence of every tag below found in them (all big-endian):
//       u64  asset_id;     // per-asset hash/ID (an AssetManager key,
//                            // "Ux" in the mangled RegisterAsset()
//                            // signature); not decoded further.
//       char name[36];     // ASCII resource-type tag, NUL-padded to a
//                            // fixed 36 bytes regardless of tag length --
//                            // confirmed exactly 36 for every occurrence
//                            // of every tag below across 5 samples.
//       u8   payload[...]; // tag-specific; see below. Its length is only
//                            // confirmed for the "FLUA" tag -- for every
//                            // other tag, finding the *next* record
//                            // requires knowing this tag's own payload
//                            // shape, which was NOT reverse-engineered.
//     Confirmed resource-type tag strings (recovered from dah_rls.elf's
//     AssetHandler registrations, then confirmed present verbatim, in this
//     exact 36-byte-padded shape, in real .stream payloads): TEXTUREDICT,
//     TEXTUREREF, SOUNDBANK, SOUNDREF, AUDIOMARKERDATA, COLLISIONMATERIALS,
//     COLLISIONMESH, CUTSCENEDATA, RENDERWORLD, SHELLDATA, SPLINE, FLUA.
//     Two more handler classes exist in the executable (AnimHandler,
//     RenderObjectHandler, TextResource) whose literal tag string was NOT
//     found/confirmed in this pass -- "ANIM" is a plausible guess for the
//     first (a "PCPLANIM" sub-tag was seen following it, see below) but
//     its measured header width didn't match the confirmed 36 bytes, so
//     it is NOT listed as confirmed above.
//
//     "FLUA" is the only tag whose payload was reverse-engineered: it is
//     embedded Lua *source* (not bytecode), immediately as
//       u32  len;           // little-endian (every other integer in this
//                            // whole format is big-endian; this one
//                            // isn't) -- confirmed exact (not rounded/
//                            // padded) against 2 independent samples.
//       char text[len];     // raw Lua source, e.g. "function
//                            // GBL_SetUpLevel( levelName )\n\n\t
//                            // dah.shell_flow_init();\nend\n\n".
//     Every other tag's payload opens with a second, inner 8-byte ASCII
//     type tag of the shape "PCPL_XXX"/"PCPLXXXX" (confirmed: TEXTUREDICT
//     -> "PCPL_RSF", RENDERWORLD -> "PCPL_RTW", COLLISIONMESH ->
//     "PCPL_COL", and a "PCPLANIM" seen following an "ANIM"-tagged region)
//     followed by further binary data (for "PCPL_RSF": a small run of u32
//     fields then a long run of paired bytes that look like an index/LOD
//     table) that was NOT reverse-engineered -- this module only decodes
//     the outer envelope (DecompressDahbwuStream()); walking individual
//     resource records and their PCPL_XXX payloads is future work.
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
//
// (4) ".AUD" -- game audio (AUDIO/BGM and AUDIO/VBK). Two sub-formats,
//     distinguished purely by content (no fixed magic on either side):
//       - 19745/19938 (99.03%) validate byte-for-byte as this project's
//         existing standard Nintendo GC/Wii mono DSP-ADPCM header (see
//         IsDSP()/DecodeDSPToWAV() in lib-dsp.h): num_samples, num_nibbles
//         (confirmed to satisfy DspAdpcmNibbleCount(num_samples) exactly),
//         sample_rate (32000 in every sample checked), loop_flag/format,
//         loop_start/loop_end/current_address (2 / num_nibbles-1 / 2 in
//         every non-looping sample checked), 16 predictor coefficients,
//         gain/ps/hist1/hist2, all at the exact standard 0x60-byte mono
//         .dsp offsets -- and the file's total byte count matches
//         DspAdpcmByteCount(num_samples) + the 0x60-byte header exactly.
//         These are handled entirely by the existing FF_DSP code path;
//         nothing DAH-specific was added for them.
//       - The other 193/19938 -- large streamed background-music/ambient
//         tracks (names like "DAH3_sunnywood_explore01", "*_MIDTRO*",
//         "*AMBIENT*") plus one confirmed-valid empty (0-byte) file -- do
//         NOT parse as a valid DSP header. Interpreting the full file
//         (from byte 0, no header) as big-endian 16-bit PCM gives a low,
//         smooth sample-to-sample delta in every one of the 192 non-empty
//         samples (mean ~1189, max ~3221 out of a 16-bit range), the same
//         signature real decoded audio gives and nothing like the ~19500
//         mean delta the same test gives on a real (still-compressed)
//         DSP-ADPCM file -- confirming these are raw, unencoded PCM. The
//         sample rate is NOT stored anywhere in these files and was NOT
//         confirmed (every DSP-ADPCM sample on the disc uses 32000 Hz, but
//         that has not been verified against this sub-format). No fixed
//         magic exists for this sub-format, so it is extension-recognized
//         only, same policy as Mercury Meltdown Revolution's ".mat"/".nav"
//         above.

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

//-----------------------------------------------------------------------------
// (4) ".AUD" headerless raw BE16 PCM sub-format (the other, DSP-ADPCM,
// sub-format needs no code here -- see lib-dsp.h's IsDSP()/DecodeDSPToWAV()).
// Extension-recognized only; see note (4) above for why no magic check
// exists. IsDahbwuAudPcm() accepts any non-odd-length file (including
// empty), so callers should try IsDSP() first and only fall back to this
// when it fails, the same order this project's ".mat"/".nav"-style formats
// are meant to be tried.

int IsDahbwuAudPcm (const u8 *data, size_t size, size_t file_size);
enumError DecodeDahbwuAudPcm_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // SZS_LIB_DAHBWU_H
