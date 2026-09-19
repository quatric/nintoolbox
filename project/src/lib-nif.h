// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Gamebryo .nif files, Wii build 20.6.0.0 (Pocoyo Racing).
//
// Layout (verified against the retail files, every block size checks out):
//   "Gamebryo File Format, Version 20.6.0.0\n", u32le version 0x14060000,
//   u8 endian (0 = big), u32le user version, u32le block count; from here
//   everything is big-endian: u16 type count, types (u32 len + chars; generic
//   types carry \x01-separated template arguments), u16 type index per block,
//   u32 size per block, u32 string count, u32 max length, strings (u32 len +
//   chars), u32 group count + sizes. Blocks follow back to back, then u32
//   root count + root references.
//
// Wii specifics used here:
//   NiPersistentSrcTextureRendererData (platform 4): pixel format at 0, the
//   palette reference at 0x3b, then u32 mip count, u32 bytes per pixel, one
//   {w, h, offset} per mip, u32 byte count, u32 pad, u32 faces, u32 platform
//   and the GX tiled pixel data (format 4 = DXT1 stored as CMPR, format 1 =
//   RGBA stored as RGBA8).
//   NiMesh: its NiDataStreams carry the geometry. A "DISPLAYLIST" stream is a
//   GX display list whose vertices are u16 indices, one per POSITION / NORMAL
//   / COLOR / TEXCOORD semantic in stream order (each attribute indexes its own
//   stream). Meshes without one (semantic INDEX) use a u16 triangle list.
//   Skinned meshes are exported in their bind pose (POSITION_BP).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_NIF_H
#define SZS_LIB_NIF_H 1

#include "types.h"
#include "lib-model-glb.h"

typedef struct nif_t nif_t;

// Returns NULL if DATA is not a big-endian Gamebryo 20.6.0.0 file.
nif_t *NifOpen (const u8 *data, uint size);
void NifClose (nif_t *nif);

// Textures: one entry per NiSourceTexture that carries Wii pixel data.
uint NifNumTextures (const nif_t *nif);
ccp NifTextureName (const nif_t *nif, uint index); // file name without extension
enumError NifDecodeTexture (const nif_t *nif, uint index, u8 **rgba, uint *width, uint *height);

// Scene geometry in world space (Z-up converted to Y-up). PNG_NAMES has one
// entry per texture (NULL = not exported). Returns NULL without any mesh.
model_t *NifBuildModel (const nif_t *nif, ccp *png_names);

#endif
