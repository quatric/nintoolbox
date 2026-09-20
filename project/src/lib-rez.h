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
// A slot's group id is its index plus the footer's first slot (texture ids in
// batches are group id << 16 | resource index).
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
// Type 75 is a depth-first tree of model objects (the game's
// GC_InitializeModelBuffer): a 224-byte header {u32 flags (0x7843, low bits
// 0x43), +8 display list bytes, +0x10 positions, +0x18 colours, +0x20
// normals, +0x28 UVs, +0x30 batches, +0x38 children, +0x44 skeleton flag,
// +0x48 f32 rest position, +0x54 rest quaternion xyzw, +0x64 rest scale,
// +0x70 pivot}, then in file order: the display list, positions f32x3,
// colours (4 bytes), normals f32x3, [positions x 16 bytes if flags & 0x100],
// UVs f32x2, batches {u32 texture id (group << 16 | index), 0, display list
// offset, display list length, 0}, padding to 32 bytes (always 1..32), then
// the children. The display list is GX (0x98 strip, 0x90 triangles, 0x80
// quads, 0xa0 fan; u16 count) with one index per position, normal, colour
// and UV (in that order), 1 byte each when the array has at most 256
// entries, else 2. Vertices are already in world space. Skinned models (+0x44
// set, flags & 0x100) carry per position {u8 bone[4] (0xff = none), f32
// weight[3]} right after the normals, and after the batches a skeleton
// {u32 bones, 12 bytes, 2 x bones u32 (identity)} followed by the bone tree,
// depth first, 32 bytes each {f32 offset from the parent[3], 0, u32 children,
// ...}; bone indices are that order. The bind pose has no rotation.
// Type 86 is the object animation of the model before it: a tree in the same
// order {u32 ?, u32 frames, f32 speed, u32 ptr, u32 children, u32 ptr,
// frames * {f32 position[3], quaternion[4], scale[3]}, children * u32, the
// children}. A frame replaces the object's rest transform about its pivot.
// Type 7 (a slot on its own) is a skeletal motion: {u32 frames, u32 bones, f32
// speed, u32 frame stride (12 + 8 * bones), tagged ptr}, frames at +0x14, each
// {f32 root offset[3], per bone s16 quaternion[4] xyzw / 32767}. The rotations
// replace the bind pose (none) of the skeleton of a skinned model; the root
// offset is exported relative to the first frame.
// Types 24, 44, 46, 52 are plain data and not decoded.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_REZ_H
#define SZS_LIB_REZ_H 1

#include "lib-std.h"
#include "lib-model-glb.h"

typedef enum { REZ_TEXTURE = 73, REZ_SOUND = 76, REZ_VIDEO = 1, REZ_MESH = 75, REZ_ANIM = 86, REZ_MOTION = 7 } rez_kind_t;

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

// Name of the PNG for texture resource ID (group << 16 | index), or NULL.
typedef ccp (*RezTexFunc) (void *ctx, u32 id);

// Objects of a type 75 resource as a model; NULL if there are none.
model_t *ParseRezModel (const u8 *res, size_t size, RezTexFunc texname, void *ctx);

// Add the object animation ANIM (type 86, the resource after the model) to the
// model parsed from RES. False if the two trees do not match.
// A skeleton-only model animated by the type 7 motion RES: the joints of REF
// (a skinned model of the same file) plus one animation; NULL if the bone
// count differs or RES is not a motion.
model_t *ParseRezMotion (const u8 *res, size_t size, const model_t *ref);

// Copy of the skeleton of MODEL (joints only), or NULL.
model_t *CopyRezSkeleton (const model_t *model);

bool AddRezAnimation (model_t *model, const u8 *res, size_t size, const u8 *anim, size_t anim_size);

// 16-bit PCM WAV of a type 76 resource.
enumError DecodeRezSound (u8 **wav, size_t *wav_size, const u8 *res, size_t res_size);

#endif
