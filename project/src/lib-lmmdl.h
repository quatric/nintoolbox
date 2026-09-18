// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Luigi's Mansion (GameCube) actor model (.mdl, magic 0x04B40000).
//
// Re-implemented in C from KillzXGaming/MdlConverter's GCNLibrary/LM/MDL
// (MIT-licensed, tool by KillzXGaming; model research by opeyx and
// SpaceCats; see https://github.com/KillzXGaming/LM_Research/wiki/MDL).
// No upstream code is copied: the layout below was recovered from the
// parser/section structure (MDL_Parser, Node, Shape, ShapePacket, Material,
// TevStage, Sampler, TextureHeader, DrawElement) and re-expressed against
// this project's own model_t/GLB pipeline.
//
// Big-endian file, 128-byte header of counts + absolute offsets, then GX
// display-list packets (opcodes 0x90 triangles / 0x98 strips / 0xA0 fans),
// a matrix table of inverse-bind 3x4 affines, a weight table for smooth
// skinning, and embedded GX textures. Each DrawElement (material, shape)
// becomes one model_t mesh; rigid vertices are baked to world space exactly
// as the reference converter does, smooth vertices keep file-space positions
// with weight-table influences.
//-----------------------------------------------------------------------------
#ifndef LIB_LMMDL_H
#define LIB_LMMDL_H 1

#include "types.h"
#include "lib-model-glb.h"

// True when DATA looks like a Luigi's Mansion actor model.
bool IsLMMDL (const u8 *data, size_t size);

// Parse to a heap model_t (free with FreeModel). NULL on invalid input.
// Also exports embedded textures? No: Parse only builds geometry/materials
// with texture names ("TextureN.png"); DecodeLMMDL writes the sibling PNGs.
model_t *ParseLMMDL (const u8 *data, size_t size);

// Decode to GLB/DAE at OUT_PATH (format by extension), writing embedded GX
// textures as sibling PNGs. Returns ERR_NOTHING_TO_DO when not an LM MDL.
enumError DecodeLMMDL (const u8 *data, uint size, ccp out_path);

// Encode MODEL to a canonical LM MDL byte stream (same section order the
// reference converter writes: packets, textures, materials, samplers,
// shapes, draw elements, packet structs, texture offsets, positions,
// normals, colours, texcoords, nodes, matrices, weights). Geometry,
// hierarchy, materials and skinning round-trip; embedded texture *pixels*
// are written zero-filled (headers/dimensions/formats preserved) because
// this codebase has no GX texture encoder -- the same documented limitation
// as the HSF/BNFM encoders, which omit texture pixels entirely.
enumError EncodeLMMDL (const model_t *model, u8 **out, uint *out_size);

// Encode MODEL straight to a file.
enumError EncodeModelToLMMDL (const model_t *model, ccp out_path);

#endif // LIB_LMMDL_H
