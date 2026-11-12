// SPDX-License-Identifier: GPL-2.0+
// Eko Software Wii engine formats (many RKET payloads carry an "EKO" signature). According to the disc file
// listings the same per-level file family ships in Aqua Panic!, SPRay, Cyberbike - Cycling Sports, The
// Garfield Show - Threat of the Space Lasagna, Bratz Kidz, Drawsome! and Adibou et les Saisons Magiques, so
// the formats are named after their own content, not after one game. Every rule below was checked against
// every file of that extension on
// the Aqua Panic! (USA), SPRay (USA) and Cyberbike (USA) discs (.rck/.spa 156, .mat 136, .mb2 136,
// .vis 136, .lit 136), and against the unrelated files that share these extensions on other discs
// ($1,000,000 Pyramid .mat, Kevin VanDam Big Bass Challenge "VBIN" .lit, Target: Terror .vis), none of
// which is accepted.
//
// Not covered: .sts, .ins, .gel, .col (magic-less per-level blobs without a confirmed record layout) and
// .ima (headerless audio).
//
// (1) ".rck" / ".spa" -- "RKET" resource container, fixed 56-byte header:
//       char magic[4];      // "RKET"
//       u32  zero;
//       u32  hash;          // per-file value
//       u16  version;       // 0x0100 / 0x0200 / 0
//       u16  flags;
//       u32  reserved;
//       u32  payload_size;  // file_size == 56 + payload_size when the payload is stored, else the unpacked size
//       u8   reserved2[12];
//       u32  kind;          // small integer
//       u8   reserved3[8];
//       u8   fill[8];       // always eight 0xAF bytes
//     The payload starts at 56.
//
// (2) ".mat" -- "MATF" material table: "MATF", u32 8, u32 count_a, u32 count_b, then {char tag[4], u32 size,
//     u8 payload[size]} chunks ("MAT " 72-byte material records, "TEX " texture records) running exactly
//     to the end of the file. Chunk payloads are not decoded.
//
// (3) ".mb2" -- "BNAM" name table: "BNAM", u32 file_size - 8, u32 count, u32 1, u8 0, then length-prefixed
//     NUL-terminated names (u32 len including the NUL) running exactly to the end of the file.
//
// (4) ".vis" -- visibility lists, no magic. One block per object, running exactly to the end of the file:
//       u32 group_count;    // >= 1
//       u32 entry_count;    // sum of the groups' counts
//       group_count x { u32 group, u32 count, count x { u32 object, u32 value } }
//     A one-object level is the 24-byte file 1, 1, {0, 1, {0, 0}}.
//
// (5) ".lit" -- light list: u32 0x20031126 (tool date tag), u32 count, count x 36-byte lights, u32 0;
//     file_size == 12 + 36 * count. A light is {u32 id (0x01xxxxxx), u32 0, f32 x, y, z, f32 r, g, b (0..255),
//     f32 param}.

#ifndef SZS_LIB_AQUAPANIC_H
#define SZS_LIB_AQUAPANIC_H 1

#include "types.h"
#include <stdio.h>

// All detectors take the buffered bytes (SIZE) and the real file size (FILE_SIZE, >= SIZE). Size equations
// use FILE_SIZE; walks that must end exactly at the end of the file only run when the whole file is buffered.

int IsAquaPanicRket (const u8 *data, size_t size, size_t file_size);
enumError DecodeAquaPanicRket_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

int IsAquaPanicMat (const u8 *data, size_t size, size_t file_size);
enumError DecodeAquaPanicMat_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

int IsAquaPanicMb2 (const u8 *data, size_t size, size_t file_size);
enumError DecodeAquaPanicMb2_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

// Magic-less: only accepted when the whole file is buffered and the block walk ends exactly at its end.
int IsAquaPanicVis (const u8 *data, size_t size, size_t file_size);
enumError DecodeAquaPanicVis_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

int IsAquaPanicLit (const u8 *data, size_t size, size_t file_size);
enumError DecodeAquaPanicLit_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // SZS_LIB_AQUAPANIC_H
