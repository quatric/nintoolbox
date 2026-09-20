// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Humongous / Cat Daddy "Resource.rez" (Backyard Football '10, Wii).
//
// Everything is big-endian. The last 2048-byte sector is a footer
//   +4 u32 first slot   +8 u32 slot count N
// and the N 24-byte slots sit in the sectors just before it (padded to 2048).
// A slot is {u32 offset, u32 size, u32 unpacked size, s16 type, u16 flags,
//            u32 0, u32 set tag}. A "group" slot has flags >= 0x20 (the byte
// size of its resource table); the table occupies the last FLAGS bytes of the
// group's data {u32 count, 12 bytes 0, count * slot, NUL-padded group name}.
// Resource slots use the same layout; flags bit 0 = compressed. The offset is
// absolute in the file. Compression (Release.elf: Decompress) is
//   u32 unpacked size, then commands: byte B, N = B & 0x7f,
//   B & 0x80 : copy N literal bytes;   else u16 distance, copy N bytes from
//   OUT - distance (may overlap).
// Type 76 is a mono DSP-ADPCM sound: u32 6, u32 sample rate, u32 0, u32 data
// bytes, s16 coefficients at +0x44, ADPCM frames from +0x80. Slots that are
// not groups (flags < 0x20) are resources on their own; type 1 slots holding
// "THP" data are videos.
// Type 73 is a texture: u16 width, u16 height, u16 (size hint), u16 GX format
// (5 RGB5A3, 6 RGBA8, 8 C4, 9 C8), then pixels at +0x80 (paletted, RGB5A3
// palette right after the pixels) or +0x60 (direct). Any mip levels follow.
// Type 75 is a run of geometry objects {u32 0x7843, u32 ?, u32 display list
// bytes, {u32 tagged ptr, u32 count} for the display list (count = position
// count), positions f32x3 (+0x14), {0, 0}, {u32 normal count, ptr} f32x3,
// {u32 uv count, ptr} f32x2, ...}. Pointers are 0x010b0000 | offset + 0x20
// into the unpacked resource. Arrays follow the display list in order
// (positions, normals, uvs). The display list is GX (0x98 strip, 0x90
// triangles, 0x80 quads, 0xa0 fan; u16 count) with one index per position,
// normal and uv, 1 byte each when the array has at most 256 entries, else 2.
// Types 7 / 24 (animation) and 44 / 46 / 52 / 86 are not decoded.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_REZ_H
#define SZS_LIB_REZ_H 1

#include "lib-std.h"
#include "lib-model-glb.h"

typedef enum { REZ_TEXTURE = 73, REZ_SOUND = 76, REZ_VIDEO = 1, REZ_MESH = 75 } rez_kind_t;

typedef struct
{
	uint group, index;
	rez_kind_t kind;
	u32 offset, csize, usize;
	uint flags;
} rez_res_t;

bool IsHumongousRez (const u8 *data, size_t size);

// Textures, sounds and videos of a Resource.rez; the array is malloc'd.
rez_res_t *ListRezResources (const u8 *data, size_t size, uint *count);

// The resource unpacked (malloc'd); NULL on error.
u8 *LoadRezResource (const u8 *data, size_t size, const rez_res_t *r, size_t *out_size);

enumError DecodeRezTexture (u8 **rgba, uint *width, uint *height, const u8 *res, size_t res_size);

// Name of the PNG texturing the mesh, or NULL.
typedef ccp (*RezTexFunc) (void *ctx);

// Geometry objects of a type 75 resource as a model; NULL if there are none.
model_t *ParseRezMesh (const u8 *res, size_t size, RezTexFunc texname, void *ctx);

// 16-bit PCM WAV of a type 76 resource.
enumError DecodeRezSound (u8 **wav, size_t *wav_size, const u8 *res, size_t res_size);

#endif
