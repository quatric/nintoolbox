// SPDX-License-Identifier: GPL-2.0+
// "Battle of the Bands" (Wii) proprietary ".bag" asset container. No public
// documentation of this format was found (checked Xentax/ZenHax archive
// mirrors, GitHub, and Wii modding wikis via web search -- nothing turned
// up for this specific title or "BAG" container). Reverse-engineered from
// scratch against the retail disc's files/*.bag (215 samples spanning
// ~100 bytes to several MB: weapon/prop data, level geometry, HUD/scene
// objects, audio-adjacent banks, texture manifests, a global asset bank).
//
// Confirmed file layout, consistent across every one of the 215 samples:
//   char header[32] = "1.00 <N>\n" (ASCII, decimal N), zero-padded to a
//                      fixed 32-byte field. N's exact meaning was not
//                      pinned down (it is NOT simply "payload length" --
//                      only true for 5/215 samples; for the rest N is
//                      smaller than the payload by a title-specific
//                      amount with no single formula found across
//                      categories), so it is reported but not relied on
//                      to delimit anything.
//   u8 payload[file_size - 32]
//
// What IS reliably decoded, and confirmed present in all 215/215 samples:
// an embedded plain-text manifest of "name,size,offset\n" CSV records
// (offsets are absolute from the start of the file; the first entry's
// offset is always exactly 32, i.e. the byte right after the header),
// bracketed by runs of '*' characters. This lists the named sub-resources
// packed into the payload (GFX/texture blobs, .xmb build-script blobs,
// etc.) -- e.g. in "legal.bag": "legal.gfx,16983,32", "legal_i5.tex,
// 22056,17024", ... "legal.xmb,139,83520". Many of the listed blobs are
// themselves raw zlib streams (magic 0x78 0x5e) -- confirmed by
// successfully inflating them at their declared offsets in multiple
// samples across categories (e.g. "buildall.bag", "hudsceneobjects.bag").
// Others (e.g. texture/".tex" blobs, and large level-geometry payloads
// like "levelarena*.bag") are not zlib and were not further decoded --
// their internal layout (likely a proprietary texture format / raw
// big-endian float vertex-ish data respectively) is out of scope here.
//
// This module decodes, for every sample: the 32-byte text header, every
// recovered manifest record (name/size/offset) in file order, and -- for
// each record whose declared offset begins a valid zlib stream -- confirms
// it inflates and reports the decompressed size. It does not claim to
// fully parse the binary layout of every embedded sub-resource type.

#ifndef SZS_LIB_BOTBBAG_H
#define SZS_LIB_BOTBBAG_H 1

#include "types.h"
#include <stdio.h>

int IsBotbBag (const u8 *data, size_t size, size_t file_size);
enumError DecodeBotbBag_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // SZS_LIB_BOTBBAG_H
