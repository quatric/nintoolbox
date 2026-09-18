// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Luigi's Mansion (GameCube) room/stage model (.bin, version 2).
//
// Re-implemented in C from KillzXGaming/MdlConverter's GCNLibrary/LM/BIN
// (MIT-licensed, tool by KillzXGaming; model research by opeyx and
// SpaceCats). Layout recovered from BIN_Parser, SceneGraphNode,
// ShapeBatch, Material, Sampler, Texture and DrawElement and re-expressed
// against this project's own model_t/GLB pipeline.
//
// Big-endian file, 64-byte header {u8 version=2, char[11] name, 13x u32
// section offsets}, then fixed-stride section arrays (texture 12B, sampler
// 20B, material 40B, shape-batch 24B, scene-graph 140B) plus absolute
// float/s16 vertex pools and GX display-list packets (0x90/0x98/0xA0).
// Section counts are implicit: everything reachable from scene-graph node 0
// through child/sibling links and draw-element indices. Each (node,
// drawn-part) pair becomes one model_t mesh; node TRS (degrees, ZYX -- the
// same convention the LM MDL importer uses) is baked into world space on
// import, exactly like the reference converter.
//-----------------------------------------------------------------------------
#ifndef LIB_LMBIN_H
#define LIB_LMBIN_H 1

#include "types.h"
#include "lib-model-glb.h"

// True when DATA looks like an LM room model (version byte + sane offsets).
bool IsLMBIN (const u8 *data, size_t size);

// Parse to a heap model_t (free with FreeModel). NULL on invalid input.
model_t *ParseLMBIN (const u8 *data, size_t size);

// Decode to GLB/DAE at OUT_PATH (format by extension), writing embedded GX
// textures as sibling PNGs. ERR_NOTHING_TO_DO when not an LM BIN.
enumError DecodeLMBIN (const u8 *data, uint size, ccp out_path);

// Encode MODEL to a canonical LM BIN byte stream. Geometry, hierarchy,
// materials and sampler/texture headers round-trip; embedded texture pixels
// are written zero-filled (same documented limitation as EncodeLMMDL) and
// node render flags come back zero (model_t carries no such field).
enumError EncodeLMBIN (const model_t *model, u8 **out, uint *out_size);

// Encode MODEL straight to a file.
enumError EncodeModelToLMBIN (const model_t *model, ccp out_path);

#endif // LIB_LMBIN_H
