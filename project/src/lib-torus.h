// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Torus Games "hunkfile" (.hnk, Wii; Barbie & Her Sisters: Puppy Rescue).
//
// A .hnk is a flat run of little-endian chunks {u32 size, u16 kind, u16 0}
// followed by SIZE payload bytes, ending exactly at end of file. Bits 12..15
// of KIND select a memory pool; the low 12 bits are the record type.
//   0x070      file header (memory pool sizes)
//   0x071      asset name: u16 1, u16 type id, u16 chunk count, u16 len0,
//              u16 len1, then two NUL-terminated strings (class, asset name)
//   0x072      end of asset
// class "TSETexture": chunk 0x150 = header (fields big-endian: +12 u16 width,
//   +14 u16 height, +26 u8 mip count), chunk 0x151 = GX pixels with the whole
//   mip chain (CMPR, RGBA8 or I8, told apart by the chain size).
// class "SqueakStream": chunk 0x092 = stream header (0x49574152 tag, +4 u8
//   channels at +6, +8 u32 samples, +12 u32 rate, DSP-ADPCM coefficients as
//   s16 at +64 (channel 2 at +112), all big-endian), chunk 0x093 = u32
//   0x40 [, u32 0x70 for stereo], then the NUL-terminated name of the raw
//   file in ../SOUND. Raw files are plain DSP-ADPCM; stereo stores channel 1
//   in the first half of the file and channel 2 in the second.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_TORUS_H
#define SZS_LIB_TORUS_H 1

#include "lib-std.h"

typedef enum { TORUS_TEXTURE = 1, TORUS_STREAM = 2 } torus_kind_t;

typedef struct
{
	torus_kind_t kind;
	char name[128];
	// texture
	uint width, height, gx_format;
	const u8 *pixels;
	size_t pixel_size;
	// stream
	uint channels, samples, rate;
	const u8 *header;
	size_t header_size;
	char raw[64];
} torus_asset_t;

bool IsTorusHnk (const u8 *data, size_t size);

// Textures and streams of a hunkfile; the array is malloc'd, points into DATA.
torus_asset_t *ListTorusHnk (const u8 *data, size_t size, uint *count);

enumError DecodeTorusTexture (u8 **rgba, const torus_asset_t *a);

// RAW: the stream's raw file. Result is a 16-bit PCM WAV.
enumError DecodeTorusStream (u8 **wav, size_t *wav_size, const torus_asset_t *a, const u8 *raw, size_t raw_size);

#endif
