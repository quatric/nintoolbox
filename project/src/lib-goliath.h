// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Goliath engine "GS" package files (*.pkz).
//
// Seen in *Skylanders: SuperChargers Racing* (Wii, USA), whose DATA partition
// carries its entire asset set as 782 files/Data/*.pkz packages next to a
// goliath.engine.wii.final.elf; every one of them starts with the same
// 0x80000001 chunk and carries a build stamp plus a "GS version 9.0.175008"
// string in its first payload chunk.
//
// NOTE: unrelated to the PlatinumGames ".pkz" archive already supported by
// this repo (lib-pkz.c, magic "pkz\0"). The two share only the extension, so
// this scanner is gated on the 0x80000001 chunk magic and on a complete,
// exact structural walk of the chunk tree, and declines anything else.
//
// Container: a plain hierarchical chunk tree, all fields big-endian, with a
// uniform 16-byte chunk header and no global directory at all:
//
//   0x00 u32 id            chunk type; the high bit is always set
//   0x04 u16 version       per-type record version
//   0x06 u16 has_children  0 = payload is opaque, 1 = payload is more chunks
//   0x08 u32 size_hi       high word of the payload size (0 in every sample)
//   0x0c u32 size          payload byte length, not counting this header
//   -- payload follows immediately, and the next sibling starts right after --
//
// The whole file is a single 0x80000001 chunk whose payload size is exactly
// filesize-16, so the walk is self-checking: every level must consume its
// parent's payload to the byte. Verified against all 782 retail packages:
// 782/782 walk cleanly, 586496 chunks, 216 distinct (id, version, children)
// combinations, zero ragged or overflowing levels.
//
// Named resources: chunk 0x8000138d is the generic "resource" wrapper. Its
// first child is always 0x8000138e, a 92-byte record holding a hash at 0x00
// and a NUL-terminated name in a 64-byte field at 0x1c ("Warnado_C",
// "VFXBigGlow02_DA[Mid]", ...). The wrapper's remaining children are the
// typed description of that resource. 73900 names were recovered this way.
//
// Bulk payloads do NOT live inside the resource wrapper: texture pixels
// (0x80000195) and audio streams (0x80001134) are pooled as children of
// 0x80000026 elsewhere in the tree. They are matched to their descriptions
// positionally, in document order, which is exact across the whole corpus
// (7055 texture headers vs 7055 texture payloads, 4994 audio descriptions
// vs 4994 audio payloads, in all 782 files).
//
// Textures: 0x8000138d -> 0x80000191 -> 0x80000197, a 48-byte header:
//
//   0x00 u32 height       height first: with the two swapped, every
//   0x04 u32 width        non-square mip chain comes out the wrong length
//   0x08 u32 mipmaps      only the low byte is the level count (40 retail
//                         headers store 0x107 for a 7-level texture)
//   0x0c u32 format       see below
//   0x10 u32 nominal_size byte length of the *colour* mip chain alone
//   0x14..0x2f            sampler state (wrap/filter/LOD bias) and a packed
//                         u16 pair, not needed for decoding
//
// The paired 0x80000195 payload is a GameCube-native (tiled) mip chain:
//
//   format 2  RGB5A3, 16bpp, full mip chain          (3 retail textures)
//   format 3  CMPR (GameCube DXT1), full mip chain   (3223)
//   format 4  CMPR mip chain immediately followed by a second, independent
//             I8 mip chain holding the alpha channel (3805). Confirmed both
//             by size arithmetic on every sample and visually: the I8 plane
//             of a Whirlwind feather sheet is exactly the feather silhouette.
//   format 5  unresolved (19 textures): the payload is neither the CMPR nor
//             the 16bpp chain its header implies, and no arrangement tried
//             decoded to a plausible image, so it is declined rather than
//             guessed at.
//
// Five mipped format-2 textures also fail their own size check (the payload
// is one base level larger than an RGB5A3 chain) and are declined the same
// way. Accepted formats are re-derived from (width, height, levels) and must
// match the payload length exactly, so a misread header can never produce a
// wrong image: 7031 of 7055 retail textures (99.66%) qualify.
//
// Only mip level 0 is exported, wrapped in a one-image TPL so the existing
// GameCube texture decoder turns it into a PNG. Format 4's alpha plane is
// emitted beside the colour image as a separate I8 TPL.
//
// Audio: 0x8000138d -> 0x80001133 describes a stream; the payload pooled at
// 0x80001134 is a big-endian RIFF ("RIFX") WAVE file whose fmt chunk uses
// wFormatTag 2 with 4 bits/sample and a long extension carrying Nintendo
// DSP-ADPCM state -- the GameCube codec, not MS-ADPCM:
//
//   ext 0x00 u32 flags
//   ext 0x04 u16 unknown
//   ext 0x06 u32 total samples
//   ext 0x0a + 44*ch: per channel, 16 big-endian s16 predictor coefficients
//            followed by 12 bytes of gain/predictor-scale/history state
//
// cbSize is therefore 10 + 44*channels rounded up (58 for mono, 104 for
// stereo in every retail sample). Stereo streams interleave the two channels
// in blockAlign/channels (32) byte blocks. Each channel is rewritten as a
// standalone .dsp so the existing DSP-ADPCM decoder produces a WAV; stereo
// streams yield one .dsp per channel. 4994 retail streams, all wFormatTag 2
// (4768 mono, 226 stereo).
//
// Extract-only; the remaining chunk types (geometry, materials, scripts and
// the engine's own runtime tables) are not interpreted here.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_GOLIATH_H
#define SZS_LIB_GOLIATH_H 1

#include "lib-nintendo.h"

// True if 'data' is a structurally complete Goliath "GS" package.
bool IsGoliathPKZ (const u8 *data, size_t size);

// Extract textures (as TPL), audio (as DSP) and a resource manifest.
enumError ScanGoliathPKZ (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif // SZS_LIB_GOLIATH_H
