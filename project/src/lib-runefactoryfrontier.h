// SPDX-License-Identifier: GPL-2.0+
// "Rune Factory: Frontier" (Wii, USA disc) proprietary asset formats.
//
// This game ships a large in-house "HX"-magic physics/collision/animation
// engine family (file extensions .hvt/.Hvt/.Hvc/.Hvb/.Hvm/.Hvh/.Hvg/.Hmt),
// plus a separate "FBTI"-magic model/motion container family (.Mod/.Mot).
// No public documentation or decompilation of this engine is known; all of
// the following was derived purely from hexdumping real disc samples.
//
// --- Shared "HX" header (all HX* sub-formats) ---
//   offset 0x00  8 bytes  ASCII tag, e.g. "HXTB0001" -- 4-letter type code
//                         followed by a 4-digit ASCII version number.
//   offset 0x08  u32 BE   type-specific meaning (see below); for HXTB it is
//                         the per-entry record stride (always 0x20 in every
//                         sample seen).
//   offset 0x0c  u32 BE   type-specific count/index field.
//   offset 0x10  u32 BE   type-specific (0 in most samples seen).
//   offset 0x14  u32 BE   type-specific (0 in most samples seen).
//   offset 0x18  u32 BE   type-specific offset/size field.
//   offset 0x1c  u32 BE   type-specific offset/size field.
// Fields past offset 0x08 vary in meaning between sub-types and are dumped
// generically (not individually named) except where confirmed below.
//
// --- HXTB0001 (.hvt / .Hvt) -- "HX Table" -- CONFIRMED entry table ---
// This is the one HX sub-format whose body is fully understood:
//   header field @0x08 = entry stride in bytes (0x20 = 32 in every sample)
//   header field @0x0c = entry count
//   header field @0x18 = size of entry table region (file_size - 0x20)
//   header field @0x1c = total file size (self-referential -- useful for
//                        validating that the whole file is present)
// Immediately following the 0x20-byte header is `count` entries of 0x20
// bytes each:
//   offset 0x00  16 bytes  NUL-padded ASCII entity name, e.g. "ENEMY",
//                          "GAUGE_MHP", "CAMP_CURSORA00"
//   offset 0x10  u32 BE    hash/checksum of the name (not reverse-engineered)
//   offset 0x14  u32 BE    data region start offset (byte offset into file)
//   offset 0x18  u32 BE    data region end offset (byte offset into file)
//   offset 0x1c  u32 BE    always 0 in every sample seen
// The [start,end) byte range for each entry point into a trailing
// per-entity data blob whose internal layout is NOT reverse-engineered.
//
// --- Other HX* magics (siblings of the same family, header confirmed, body
//     not reverse-engineered beyond generic field dump + name-string scan)
// ---
//   HXCB0002 (.Hvc)              -- likely "HX Collision Box"
//   HXAA0001 / HXAB0001 (.Hvb)   -- likely "HX Animation" (bone/skeleton);
//                                   both magics observed for the same
//                                   extension across different samples
//   HXMB0001 (.Hvm)              -- likely "HX Map" or "HX Motion"
//   HXHB0001 (.Hvh)              -- likely "HX Hull/Height"
//   HXGB0001 (.Hvg)              -- likely "HX Geometry" (also seen
//                                   embedded inside FBTI .Mod containers)
//   HXTP0001 (.Hmt)              -- likely "HX Triangle/Points" -- a small
//                                   sequential index list (collision mesh
//                                   index buffer), given the paired
//                                   OBJ_CM_TRIANGLE.Hvg/.Hvh referenced by
//                                   the companion Triangle.def manifest
// For all of the above, only the generic 0x20-byte header is decoded, plus
// a best-effort scan for embedded printable ASCII entity/name strings
// (mirroring the readable tags seen in HXTB/HXCB, e.g. "ENEMY",
// "BM01_DEATH"). The post-header per-type record layout is genuinely
// ambiguous and is NOT guessed at.
//
// --- FBTI0001 (.Mod / .Mot) -- CONFIRMED section table ---
// Both extensions share the exact same container shell (likely "Frontier
// Binary Table/Info" or similar in-house tag -- no attempt made to
// identify the real studio's own name for it). Header:
//   offset 0x00  8 bytes  ASCII magic "FBTI0001"
//   offset 0x08  u32 BE   section count
//   offset 0x0c  u32 BE   header size in bytes (always 0x10 = 16 in every
//                         sample seen -- i.e. the section table starts
//                         right after this field)
// Immediately following is `count` entries of 8 bytes each:
//   offset 0x00  u32 BE   section byte offset (from start of file)
//   offset 0x04  u32 BE   section byte size
// Verified across every sample: consecutive sections are contiguous
// (offset[i] + size[i] == offset[i+1]), and the final section's
// offset + size == the file's total size. In .Mod files the first section
// commonly begins with an embedded HXGB0001 (or other HX*) sub-resource,
// confirming FBTI is a generic outer container wrapping HX-family payloads
// plus (presumably) raw vertex/animation-frame data; the internal layout of
// each section's payload beyond that leading HX header is NOT
// reverse-engineered.

#ifndef SZS_LIB_RUNEFACTORYFRONTIER_H
#define SZS_LIB_RUNEFACTORYFRONTIER_H 1

#include "lib-std.h"
#include <stdio.h>

//-----------------------------------------------------------------------------
// (1) HXTB0001 -- "HX Table" entry directory (.hvt / .Hvt)

int IsRFFHxtb ( const u8 *data, size_t size, size_t file_size );
enumError DecodeRFFHxtb_Text ( FILE *f, const u8 *data, size_t size, size_t file_size );

//-----------------------------------------------------------------------------
// (2) Other confirmed "HX" family magics -- header-only decode

int IsRFFHxcb ( const u8 *data, size_t size, size_t file_size ); // HXCB0002 (.Hvc)
int IsRFFHxaa ( const u8 *data, size_t size, size_t file_size ); // HXAA0001/HXAB0001 (.Hvb)
int IsRFFHxmb ( const u8 *data, size_t size, size_t file_size ); // HXMB0001 (.Hvm)
int IsRFFHxhb ( const u8 *data, size_t size, size_t file_size ); // HXHB0001 (.Hvh)
int IsRFFHxgb ( const u8 *data, size_t size, size_t file_size ); // HXGB0001 (.Hvg)
int IsRFFHxtp ( const u8 *data, size_t size, size_t file_size ); // HXTP0001 (.Hmt)

// Shared generic header + name-string-scan decoder for all of the above.
enumError DecodeRFFHxGeneric_Text ( FILE *f, const u8 *data, size_t size, size_t file_size );

//-----------------------------------------------------------------------------
// (3) FBTI0001 model/motion section container (.Mod / .Mot)

int IsRFFFbti ( const u8 *data, size_t size, size_t file_size );
enumError DecodeRFFFbti_Text ( FILE *f, const u8 *data, size_t size, size_t file_size );

#endif // SZS_LIB_RUNEFACTORYFRONTIER_H
