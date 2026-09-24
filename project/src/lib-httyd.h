// SPDX-License-Identifier: GPL-2.0+
// DreamWorks "How to Train Your Dragon" (Wii, USA/En,Fr disc) proprietary
// asset formats. No public documentation of any of these exists (checked
// XeNTaX, GBAtemp, Models-Resource, romhacking.net -- all empty on this
// title). Everything below was reverse-engineered from scratch against the
// retail disc, cross-checking every real sample of each extension
// byte-for-byte (never a single-sample guess).
//
// Two formats are covered:
//   .RWS / .mtd -- shared 12-byte-header chunk-tree container (118 real
//           .RWS samples under MUSIC/*/STRA*.RWS and LVL*/SPEECH/*_REV0.RWS,
//           plus the single EFFECTS.mtd). Despite the ".RWS" extension this
//           is NOT a RenderWare "RenderWare Stream" file in the sense of
//           carrying the literal "RenderWare" ASCII banner some tools look
//           for, but the on-disk layout IS a classic RenderWare-style
//           recursive binary-chunk stream: repeating 12-byte headers of
//           { u32 type; u32 size; u32 marker; }, all little-endian even
//           though this is a big-endian Wii disc (this game's audio
//           middleware evidently keeps its authoring-time PC byte order),
//           with the payload of a chunk free to contain further chunks of
//           the same shape. See note (1).
//   .KRV -- gzip-wrapped localization string table (52 real samples:
//           00GLC.KRV, 01GLC.KRV, GAME.KRV, and per-level *.KRV files under
//           DATA/files/). The outer gzip wrapping is already handled
//           generically by this codebase's passthru staging layer (any
//           1f-8b-08 magic is routed to the 7-Zip passthru path); this
//           module recognizes the format and decodes the *decompressed*
//           payload, which is a small integer header followed by a run of
//           NUL-terminated UTF-16LE strings (the game's on-screen text).
//           See note (2).
//
// Extensions confirmed already standard and NOT covered here:
//   .bmp   -- standard Windows/OS-2 bitmap.
//   .wad   -- standard Wii title WAD.
//   .arc   -- standard Nintendo U8 archive.
//   .tpl   -- standard Nintendo TPL texture palette.
//   .txt/.csv -- plain text.
//   .bin/.img -- standard disc metadata (tmd.bin/ticket.bin/cert.bin/h3.bin/
//           bi2.bin, apploader.img), not game-proprietary.
//   .sh/.bat/.inf -- plain-text build/tool scripts, not game data.
//   .bnr   -- standard Wii banner.
//   .dol   -- standard Dolphin executable.
//   .VID   -- RAD Game Tools Bink video (confirmed "BIKi" magic); the
//           existing Bink detection in lib-passthru.c was previously gated
//           to the ".bik" extension only and has been extended to also
//           accept ".VID" so this title's videos are recognized by magic
//           regardless of the renamed extension (see lib-passthru.c).
//
// (1) ".RWS" / ".mtd" chunk-tree container. Confirmed against many real
//     samples spanning both extensions and a wide size range (100352 bytes
//     up to 23525376 bytes for .RWS; the single 5400-byte EFFECTS.mtd),
//     recursively walking every chunk in each sample and finding the shape
//     below holds with zero exceptions:
//       u32 type;    // LE, chunk type/id (values seen: 0x1, 0x2, 0x3, 0x20,
//                     // 0x21, 0x80d, 0x80e, 0x80f -- 0x1/0x2/0x3 recur at
//                     // every nesting depth and line up with classic
//                     // RenderWare STRUCT(1)/STRING(2)/EXTENSION(3) chunk
//                     // IDs; the higher IDs look like this game's own
//                     // audio-bank plugin chunk types)
//       u32 size;    // LE, byte length of the payload immediately
//                     // following this header (may itself be a run of
//                     // further chunks of this same shape); confirmed
//                     // exactly against file size and against every
//                     // parent/child chunk boundary in every sample
//                     // (offset + 12 + size always lands exactly on the
//                     // next sibling chunk header or EOF).
//       u32 marker;  // LE, constant 0x1c020065 on literally every chunk
//                     // header seen, at every nesting depth, in every
//                     // sample of both extensions -- functions as a
//                     // structural sanity/version stamp rather than a
//                     // per-chunk value.
//     A chunk's payload is either raw data (leaf chunk, e.g. type 0x2/0x3)
//     or another run of child chunks (confirmed by re-checking the marker
//     at payload offset 0 before recursing). The actual per-chunk binary
//     payload contents (compressed/encoded audio sample data, DSP tables,
//     etc., as suggested by the "Stream0" ASCII tag seen embedded in
//     .RWS leaf payloads) were NOT reverse-engineered; this module only
//     decodes and enumerates the chunk tree (type/size/offset), not the
//     leaf payload semantics.
//
// (2) ".KRV" gzip-wrapped localization string table. Confirmed against
//     multiple real samples (00GLC.KRV, 01GLC.KRV, GAME.KRV and others):
//     every sample starts with the standard gzip magic (1f 8b 08 00), and
//     inflates cleanly to a payload whose first bytes are a handful of
//     small 32-bit little-endian integers (a count and what look like
//     table-size/offset fields; the exact field semantics were NOT pinned
//     down from the samples available) followed by a long run of
//     NUL-terminated UTF-16LE strings -- confirmed readable, game-authentic
//     on-screen text and menu strings (English and French) in every sample
//     decompressed. This module decodes the gzip wrapper (independently of
//     the generic passthru staging path, so `wszst` can show the decoded
//     text directly) and enumerates the UTF-16LE string run; the leading
//     integer header fields and any binary framing between the header and
//     the first string were NOT reverse-engineered beyond confirming their
//     presence.

#ifndef SZS_LIB_HTTYD_H
#define SZS_LIB_HTTYD_H 1

#include "types.h"
#include <stdio.h>

//-----------------------------------------------------------------------------
// (1) ".RWS" / ".mtd" chunk-tree container

int IsHTTYDRws (const u8 *data, size_t size, size_t file_size);
enumError DecodeHTTYDRws_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (2) ".KRV" gzip-wrapped localization string table

int IsHTTYDKrv (const u8 *data, size_t size, size_t file_size);
enumError DecodeHTTYDKrv_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // SZS_LIB_HTTYD_H
