// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "Bj" engine assets (Super Karts / Pro Kart, Wii). All little-endian (the
// engine byte-swaps them at load time).
//
// .tx1 + .tx2 texture set (tx1 = headers, tx2 = pixels):
//   tx1: u32 0xfacc00ff, u32 n_textures, u32 first record offset, ...
//   record (linked by u32 next at +0, 24 bit offset): u32 next, u32 prev,
//     u32 0, u32 tx2 location (u16 start, u16 length, in 2 KiB units),
//     u8 format (12 = 8-bit paletted), u8 frames, u16 0, u16 flags (0x80 =
//     square mip chain, 0x1000 = alpha), u16 0, u16 width, u16 height,
//     u32 palette entries, palette entries * RGBA, all stored 4 bytes.
//   tx2: 8-bit palette indices, row-major; a square 0x80 texture stores its
//   full mip chain (w*w, w/2*w/2, ...) and further frames follow.
//
// .mtm mesh tree: u32 0x80178e55, u32 version, u32 flags, u32 n_textures,
//   u32 texture list offset, u32 root node offset, u32 node table, ...
//   Node: u32 pad, u32 child, u32 sibling, ..., u32 n_meshes at +108,
//   4x4 float matrix at +112 (row vectors), u32 mesh offset table at +176.
//   Mesh: flags at +8 (bits 0-3 vertex format), texture id at +64, counts
//   at +72.., vec4 position array (+116), vertex records (+128; u16 position
//   index, optional float UV pair at +4), u16 index list (+132), primitive
//   table of 12 bytes {u16 flags (type: 4 list, 5 strip, 6 fan; 8 = indexed),
//   u16 x3, u16 count, u16 start} (+144).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_BJ_H
#define SZS_LIB_BJ_H 1

#include "lib-std.h"
#include "lib-model-glb.h"

bool IsBjTx1 (const u8 *tx1, size_t size);
uint BjTx1Count (const u8 *tx1, size_t size);
// Decode texture IDX (first frame). RGBA result is malloc'd.
enumError DecodeBjTexture (u8 **rgba, uint *width, uint *height, const u8 *tx1, size_t size1,
	const u8 *tx2, size_t size2, uint idx);

// Name of the PNG for texture ID, or NULL.
typedef ccp (*BjTexFunc) (void *ctx, uint tex_id);
bool IsBjMtm (const u8 *data, size_t size);
model_t *ParseBjMtm (const u8 *data, size_t size, BjTexFunc texname, void *ctx);


// .bsi sound bank (big-endian): u32 0x0005002d, u32 count, u32 table offset
// (0x10), then count * {u32 flags<<24, u32 0, u32 rate, u32 bytes, u32 0,
// u32 file offset, u32 0, u32 0}; each sample is mono signed 8-bit PCM.
bool IsBjBsi (const u8 *data, size_t size);
uint BjBsiCount (const u8 *data, size_t size);
enumError DecodeBjBsiSample (u8 **wav, size_t *wav_size, const u8 *data, size_t size, uint idx);

// .bsm streamed music: two planar channel halves of signed 8-bit PCM at 32 kHz
// (first half left, second half right); decodes to a stereo 16-bit WAV.
enumError DecodeBjMusic (u8 **wav, size_t *wav_size, const u8 *data, size_t size);

#endif
