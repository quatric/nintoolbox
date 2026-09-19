// SPDX-License-Identifier: GPL-2.0+
// Game Freak Nintendo 3DS formats (Pokemon X/Y, Omega Ruby/Alpha Sapphire,
// Sun/Moon era): GFL2 raw model/texture/motion blobs, GFModelPack and
// GFPackage containers, GF1 motion packs and GFLXPack archives.
//
// Reference: SPICA by gdkchan (https://github.com/gdkchan/SPICA) and the
// 3dsTools CLI wrapper by KillzXGaming
// (https://github.com/KillzXGaming/3dsTools), which converts these formats
// through the SPICA library. Magic numbers, header layouts, the PICA200
// command-buffer vertex decoding and the GFNV1 name hash below are ported
// from that library's SPICA/Formats/GFL2, SPICA/Formats/Packages/GFL and
// SPICA/Formats/GFLX trees.
#ifndef LIB_GF3DS_H
#define LIB_GF3DS_H 1

#include "lib-std.h"

// Raw blob magics (little-endian u32 at offset 0).
#define GF_MAGIC_MODEL 0x15122117u // GFModel ("gfmodel" section)
#define GF_MAGIC_TEXTURE 0x15041213u // GFTexture ("texture" section)
#define GF_MAGIC_MOTION 0x00060000u // GFMotion
#define GF_MAGIC_MODELPACK 0x00010000u // GFModelPack container

// Structural probes. Each validates beyond the bare magic so weak magics
// such as 0x00010000 never misfire on unrelated files.
int IsGFModel (const u8 *data, size_t size);
int IsGFTexture (const u8 *data, size_t size);
int IsGFMotion (const u8 *data, size_t size);
int IsGFModelPack (const u8 *data, size_t size);
// Gen6/Gen7 package: 2 ASCII uppercase bytes + u16 count + offset table.
int IsGFPackage (const u8 *data, size_t size);
// Switch-era LZ4 archive ("GFLXPACK", 8 bytes).
int IsGFLXPack (const u8 *data, size_t size);
// XY/ORAS motion pack: u32 anim count + offset table (no magic).
int IsGF1Motion (const u8 *data, size_t size);

// GFTexture (0x15041213) -> RGBA8. The pixel payload uses the same PICA200
// layouts as CTPK/BCH, so this maps GFTextureFormat to PICATextureFormat
// and reuses DecodePicaTexture.
enumError DecodeGFTexture_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, size_t size);
// Copies the texture name (0x40-byte field) into buf; empty string when absent.
void GetGFTextureName (char *buf, size_t bufsz, const u8 *data, size_t size);

// GFModel (0x15122117) -> model_t (lib-model-glb.h), typed as void* here so
// this header stays independent of the model layer. Release with FreeModel().
// Skeleton, materials (with texture names) and per-submesh geometry with
// smooth skinning are decoded; texture pixels live in sibling GFTexture
// blobs and are referenced by name only.
void *ParseGFModel (const u8 *data, size_t size);

// Human-readable manifests for `wszst xx` (see extract_*_manifest pattern).
enumError DecodeGFMotion_Text (FILE *f, const u8 *data, size_t size);
enumError DecodeGF1Motion_Text (FILE *f, const u8 *data, size_t size);
enumError DecodeGFModelPack_Text (FILE *f, const u8 *data, size_t size);

// `wszst xx` archive extraction. Members keep their section/entry names and
// get a content-sniffed extension so they can be fed back into wmdlt/wimgt.
enumError ExtractGFModelPackArchive (ccp arg, ccp basedir, uint depth);
enumError ExtractGFPackageArchive (ccp arg, ccp basedir, uint depth);
enumError ExtractGFLXPackArchive (ccp arg, ccp basedir, uint depth);

#endif // LIB_GF3DS_H
