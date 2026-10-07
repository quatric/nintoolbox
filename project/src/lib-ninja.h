// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_NINJA_H
#define SZS_LIB_NINJA_H 1

#include "lib-std.h"
#include "lib-model-glb.h"
#include <stdio.h>

// Illvelo (Wii, Sonic Team) "Ninja" chunk model/motion family (.nj/.njm,
// 962+367 samples on the retail JP disc). Structurally this is Sega's
// well-known Dreamcast-era "Ninja" engine chunk format (NJTL texture list,
// NJCM chunk model, NMDM chunk motion -- documented by the SADX/SA_Tools
// modding community for Sonic Adventure/Heroes/Shadow), reused on Wii with
// the same two systematic transformations confirmed for lib-segapvr.c:
//
//   1. Every 4-byte chunk tag is stored as the exact reverse of its
//      well-known ASCII spelling: "NJTL" -> "LTJN", "NJCM" -> "MCJN",
//      "NMDM" -> "MDMN", "NCAM" (camera motion, seen as a standalone .njm
//      root) -> "MACN", and Nintendo's own "POF0" pointer-fixup tag (also
//      present, appended after the model data) -> "0FOP".
//   2. Every multi-byte numeric field is big-endian.
//
// A .nj file is a flat sequence of top-level chunks (tag + BE size + body),
// walked here without assuming any particular order or count. Only the
// NJTL texture list body is decoded, and it is confirmed byte-exact against
// real samples:
//   u32 texlist_rel_off; // relative to the body start; byte-exact
//                         // confirmed: body_start+texlist_rel_off is
//                         // always the start of the entry array below
//   u32 tex_count;
//   struct { u32 name_rel_off; u32 global_index; u32 flags; } entry[tex_count];
//     // name_rel_off is also relative to the body start, and lands
//     // exactly on a NUL-terminated ASCII texture name in every sample.
//
// NJCM (chunk model tree) and NMDM (chunk motion) are NOT reverse-engineered
// here -- unlike the well-documented Dreamcast/PC Ninja format, this port's
// exact object-tree/vertex-chunk/motion-track layout was not verified
// against real samples, so they -- and any other unrecognized tag -- are
// reported as opaque chunks (tag + size + offset only), the same policy
// lib-g3res.c uses for its own unconfirmed leaf chunks.
// NJCM chunk models (Milestone "Chaos Field"/"Karous" .dat model banks, same
// byte-reversed-tag big-endian port). Verified against all 156 models of the
// Ultimate Shooting Collection's *Models.dat files:
//   NJCM body = NJS_OBJECT tree: {u32 evalflags, u32 model, f32 pos[3],
//     s32 ang[3] (BAMS, 0x10000 = 360 deg), f32 scale[3], u32 child, u32 sibling}
//     with body-relative pointers; flags 1/2/4 = no pos/rot/scale, 8 = hidden,
//     0x10 = skip children, 0x20 = ZXY rotation order.
//   model = {u32 vlist, u32 plist, f32 center[3], f32 radius}
//   vlist = {u16 type, u16 size_words, u32 count, vertices}; type 0x8023 =
//     {f32 xyz, u8 rgba[4]} (16 bytes), type 0x802a = {f32 xyz, f32 normal[3],
//     u8 rgba[4]} (28 bytes)
//   plist = chunks of u16 words: 0x2513 {u16 size, diffuse argb, ambient argb},
//     0x3408 {u16 0x4000|tex_index, u16 render_flags, u16 size_words,
//     u16 strip_count, strips{s16 len (<0 = reversed), len*{u16 idx, u16 u, u16 v}}},
//     0x00ff = end; UVs are 10-bit fixed point (1/1024).
// Returns a malloc'ed model_t (free with FreeModel) or NULL.
model_t *ParseNinjaModel (const u8 *data, size_t size);

int IsNinjaChunk (const u8 *data, size_t size);
enumError DecodeNinjaChunk_Text (FILE *f, const u8 *data, size_t size);

#endif // SZS_LIB_NINJA_H
