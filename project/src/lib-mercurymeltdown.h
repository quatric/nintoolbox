// SPDX-License-Identifier: GPL-2.0+
// "Mercury Meltdown Revolution" (Wii, USA/En,Fr,Es disc) proprietary asset
// formats. No public documentation of any of these exists (checked XeNTaX,
// GBAtemp, Models-Resource, romhacking.net -- all empty on this title).
// Everything below was reverse-engineered from scratch against the retail
// disc, by extracting files/ and cross-checking multiple real samples of
// each extension byte-for-byte (never a single-sample guess). Internal
// strings found in the extracted files ("L:/SOURCEDATA/...", "L:/gamedata/
// psp/data/...") show this engine started life as a PSP/PC "Mercury
// Meltdown" title and was ported to Wii, which explains why several
// formats mix little-endian fields (native to the PC/PSP tools) with
// big-endian fields (native to the Wii target).
//
// Five formats are covered, at varying depth. Measured recognition/decode
// rate across every real sample of each extension extracted from the
// retail disc: .zen 370/370 (100% outer header), .col+.cam 370+370/370+370
// (100% outer header, shared "COL0" family), .pst 372/372 (100% outer
// header), .mat and .nav are magic-less and are extension-recognized only,
// same policy as The Dog Island's .sci/.qci and Zack & Wiki's .ssd -- see
// notes (4) and (5) below.
//
// Several other extensions on this disc are NOT covered here because they
// were confirmed to already be standard, generically-supported formats,
// not proprietary to this game:
//   .thp    -- standard Nintendo THP video (already detected via FF_THP).
//   .brsar  -- standard Wii "RSAR" sound archive (already handled by
//              lib-brsar.c).
//   .wad    -- standard Wii IOS/title WAD (cert-chain + ticket + TMD
//              layout, header size 0x20), seen only under UPDATE/files/
//              _sys/ (BOOT2/IOS installers), not a game asset.
//   .arc    -- standard Nintendo "U8-" archive (magic 0x55AA382D, already
//              detected via lib-szs.h's U8_MAGIC_NUM/FileTypeTab).
//   .AT3    -- a plain RIFF/WAVE container (magic "RIFF"..."WAVE", "fmt "
//              chunk with wFormatTag 0xfffe, i.e. WAVE_FORMAT_EXTENSIBLE,
//              likely wrapping Sony ATRAC3 samples) -- the same
//              RIFF/WAVE outer shell this project already recognizes
//              generically for numerous other games (see lib-asobo.c,
//              lib-bj.c, lib-dsp.c, lib-fsb.c, etc.), so no new per-game
//              format was added for it.
//   .TEX / .matTxt -- both confirmed to be plain ASCII text manifests
//              (a "#TEX FILE ..." header followed by a list of source
//              asset paths for .TEX; a bare newline-separated list of
//              material names for .matTxt), not binary formats at all.
// .SHD, .PMH, .spt, .spd, .ptc, .PMF and .fnt were inspected briefly but
// not reverse-engineered in this pass -- see note (6).
//
// (1) ".zen" -- scene/level file. Confirmed header:
//       char magic[4];     // "DAED" (fixed, all 370 samples)
//       u32  field_a;      // LE. Observed 0x0000c040 in every sample
//                           // seen -- looks like a fixed sub-version/
//                           // flags tag, not understood beyond that.
//       u32  zero;         // LE. Always 0 in every sample seen.
//       u16  count_a;      // LE. Small integer (1 in every sample seen).
//       u16  count_b;      // LE. Small integer (0 or 3 observed).
//     Immediately followed by a NUL-terminated ASCII path string (e.g.
//     "L:/SOURCEDATA/Level_Trays/DanM/dm_EasyTray02/..." or
//     "L:/sourcedata/Skyboxes/Race_skybox3.mb"), the original authoring
//     tool's source-asset path -- confirmed present and NUL-terminated in
//     every sample. What follows the path string (further LE u32 fields,
//     hash-looking values, and float data) was NOT reverse-engineered;
//     this module only decodes the fixed header plus the source-path
//     string.
//
// (2) ".col" / ".cam" -- shared "COL0" family: level collision-mesh data
//     for ".col", and (confirmed on every ".cam" sample pulled) either an
//     empty/degenerate 12-byte instance of the very same "COL0" magic +
//     8 zero bytes (seen whenever a backdrop/skybox has no camera-collision
//     data of its own) or a larger populated instance identical in shape
//     to a ".col" file. Confirmed header:
//       char magic[4];   // "COL0" (fixed, all 370+370 samples)
//       u32  count;      // LE. Number of top-level records that follow
//                         // (0 in every empty 12-byte ".cam" sample seen).
//     Each record, where present, opens with a NUL-terminated ASCII name
//     (e.g. "tray_ColMat", "Finish01", "BouncePad" -- confirmed to match
//     names also seen in the matching level's ".matTxt" material-name
//     list) followed by small LE u32 tag/flag/index fields whose exact
//     per-field meaning was NOT pinned down (the values look like a
//     tagged property list -- repeated (tag,size,value...) triplets of
//     varying width -- but a consistent fixed-width record shape could
//     not be confirmed across samples). This module decodes the magic +
//     count and the leading name of each record when present; the
//     per-record tagged property data is reported raw, not parsed.
//
// (3) ".pst" -- paletted texture. Confirmed header:
//       char magic[4];      // "TSPA" (fixed, all 372 samples)
//       u32  field_a;       // BE. Observed 0x00000002 in every sample
//                            // seen.
//       u8   pad;           // Observed 0x20 (' ') in every sample seen.
//       char tag[3];        // "CGN" (fixed, all samples) -- together
//                            // with the preceding pad byte this reads as
//                            // an embedded " CGN" sub-tag; meaning not
//                            // understood beyond being a fixed constant.
//       u32  field_b;       // BE. Observed 0x00000010 in every sample
//                            // seen.
//       u32  width;         // BE. Confirmed against multiple samples of
//                            // known/plausible texture dimensions (e.g.
//                            // 0x40 = 64) -- not a byte count, an actual
//                            // pixel dimension, by cross-checking several
//                            // files' width*height*4 against their file
//                            // size (holds approximately, within header
//                            // + mipmap overhead).
//       u32  height;        // BE, same evidence as width.
//     What follows (a flags/mip-count word, then raw pixel-ish byte runs)
//     was NOT reverse-engineered -- looks like a proprietary paletted or
//     packed pixel format (no match found against any known Wii/GX
//     texture layout such as TPL/CMPR), so only the fixed outer header
//     above is decoded here.
//
// (4) ".mat" -- material float-record table. NOT reverse-engineered to a
//     confirmed structure: no fixed magic was found (files open directly
//     with a small BE-looking u32 "type" value -- 0xf and 0x5 observed
//     across different samples -- followed by 8 zero bytes, a 4-byte
//     value that changes per file and looks like a name hash, and then a
//     long run of IEEE-754 float32 values, many equal to 1.0
//     (0x3f800000) or small color-channel-like fractions). No end-of-
//     record marker or count field could be pinned down that held across
//     multiple samples of differing file size, so this format is
//     extension-recognized only, same policy as The Dog Island's .sci/
//     .qci and Zack & Wiki's .ssd -- it is intentionally NOT wired into
//     the magic-based auto-detector (it would false-positive on
//     arbitrary binary/float data) and is only reachable by explicit
//     extension match.
//
// (5) ".nav" -- navigation-mesh table. Also magic-less: files open with
//     small LE-looking u32 values (counts?) followed by mixed 0xffff
//     sentinel-looking u16 pairs and later float32 data, but (unlike
//     ".mat") a meaningful fraction of real samples on this disc are
//     zero-length (empty) files, and no consistent header shape held
//     across the remaining non-empty samples pulled. Extension-
//     recognized only, same policy and same reasoning as ".mat" above.
//
// (6) NOT covered in this pass: ".SHD" and ".PMH" both open with mostly-
//     zero fields and, at a fixed but *different* offset in each (offset
//     0 for .PMH, offset 0x20 for .SHD), contain the repeated 32-bit
//     pattern 0xf1c62a23, which recurs many more times later in both
//     file bodies -- almost certainly a floating-point sentinel/marker
//     constant (bit pattern of a very large or NaN-adjacent float) used
//     internally by the same middleware, rather than a file magic. Since
//     the two formats do not share a common fixed-offset header and nothing
//     about the marker's role in the file structure could be confirmed
//     from this pass, they are left entirely unimplemented here rather
//     than guessed at. Likewise ".spt"/".spd"/".ptc"/".PMF"/".fnt" were
//     not inspected in enough depth this pass to document responsibly and
//     are left for a future pass.

#ifndef SZS_LIB_MERCURYMELTDOWN_H
#define SZS_LIB_MERCURYMELTDOWN_H 1

#include "types.h"
#include <stdio.h>

//-----------------------------------------------------------------------------
// (1) ".zen" scene file

int IsMercuryZen (const u8 *data, size_t size, size_t file_size);
enumError DecodeMercuryZen_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (2) ".col" / ".cam" "COL0" collision/camera table

int IsMercuryCol (const u8 *data, size_t size, size_t file_size);
enumError DecodeMercuryCol_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (3) ".pst" "TSPA" paletted texture

int IsMercuryPst (const u8 *data, size_t size, size_t file_size);
enumError DecodeMercuryPst_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (4) ".mat" material float-record table (extension-only, unconfirmed)

int IsMercuryMat (const u8 *data, size_t size, size_t file_size);
enumError DecodeMercuryMat_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (5) ".nav" navigation-mesh table (extension-only, unconfirmed)

int IsMercuryNav (const u8 *data, size_t size, size_t file_size);
enumError DecodeMercuryNav_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // SZS_LIB_MERCURYMELTDOWN_H
