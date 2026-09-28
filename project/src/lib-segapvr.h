// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_SEGAPVR_H
#define SZS_LIB_SEGAPVR_H 1

#include "lib-std.h"
#include <stdio.h>

// Illvelo (Wii, Sonic Team) "PVR" texture, reverse-engineered from scratch
// against the retail JP disc (DATA/files/**/*.pvr, 626 samples). Structurally
// this is Sega's well-known Dreamcast/Naomi "PVR" texture container -- an
// optional 16-byte "GBIX" global-index header followed by a "PVRT" texture
// header (4-byte tag, 4-byte size, 4-byte type, u16 width, u16 height) -- but
// ported to the Wii with two systematic transformations confirmed across
// every one of the 626 samples:
//
//   1. Both magic tags are stored as the exact reverse of their well-known
//      ASCII spelling: "GBIX" -> "XIBG", "PVRT" -> "TRVP". (Consistent with
//      the original engine defining each tag as a little-endian 32-bit
//      constant built from the 4 characters; a big-endian Wii toolchain
//      writing that same constant out byte-for-byte reproduces the reversed
//      ASCII order seen on disk.)
//   2. Every multi-byte numeric field (size, type, width, height) is
//      big-endian, unlike the little-endian original Dreamcast format.
//
// Confirmed byte-exact across all 626 samples: PVRT.size always equals
// 8 + (bytes remaining in the file after width/height), i.e. it covers
// type+width+height+texel data, exactly like the original spec.
//
// The 4-byte "type" field's low byte is confirmed to take only the values
// 0/1/2, matching Sega's own PIXEL_FORMAT enum (0=ARGB1555, 1=RGB565,
// 2=ARGB4444); its high byte only takes small values (0x45/0x48/0x4e/...)
// that don't correspond to any twiddled/VQ/mipmap code (0x01/0x02/0x03/0x04)
// from the public Dreamcast PVR spec, and pixel-decoding a real font/BOSS
// sample under every twiddled/raw x big/little-endian texel combination
// produced no recognizable image -- so unlike lib-gvr.c, this decoder does
// NOT attempt an RGBA texel decode. It only decodes the (fully confirmed)
// header: GBIX global index, PVRT size, raw type word, width, height, and
// texel data size/offset -- see lib-segapvr.c.
int IsSegaPVR (const u8 *data, size_t size);
enumError DecodeSegaPVR_Text (FILE *f, const u8 *data, size_t size);

#endif // SZS_LIB_SEGAPVR_H
