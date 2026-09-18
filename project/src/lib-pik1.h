// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Pikmin 1 (GameCube) assets: chunked MOD model (.mod), TXE texture (.txe)
// and the ARC/DIR archive pair.
//
// Re-implemented in C from KillzXGaming/MdlConverter's
// GCNLibrary/Pikmin (MIT-licensed, tool by KillzXGaming). Layout recovered
// from MOD_Parser/MOD_Common (chunk opcodes), Joint/Material/Envelope/
// TextureAttribute/PolygonGroup sections, TXE and Pikmin1/ARC+DIR, and
// re-expressed against this project's own model_t/GLB pipeline. No upstream
// code is copied.
//
// MOD is a chunk stream of {s32 opcode, u32 size} records on 32-byte
// boundaries, starting with a Header chunk and ending with 0xFFFF. Vertex
// pools live at absolute offsets; meshes reference them through standard GX
// display lists (0x90 triangles / 0x98 strips / 0xA0 fans, INDEX16 into the
// pools). Skinning resolves through the SkinningIndices table (rigid joint
// ids up to a 0xFFFF separator, inverted envelope ids after it) plus the
// Envelope weight lists. Joints carry parent/position/rotation/scale
// directly. Embedded TXE textures decode to sibling PNGs.
//
// NOTE (no retail samples available): variable-length tables (texture
// attributes, joints, envelopes, skinning indices, joint names) are read as
// u32-count-prefixed arrays, matching the format's explicit-count chunks
// (textures, materials); the encoder writes the same canonical form, so
// decode -> encode -> decode is stable. If retail files turn out to use a
// different framing for those tables, the validator rejects them cleanly
// instead of misparsing.
//-----------------------------------------------------------------------------
#ifndef LIB_PIK1_H
#define LIB_PIK1_H 1

#include "types.h"
#include "lib-model-glb.h"

// True when DATA looks like a Pikmin 1 model (chunk walk validates).
bool IsPIKMOD (const u8 *data, size_t size);

// Parse to a heap model_t (free with FreeModel). NULL on invalid input.
model_t *ParsePIKMOD (const u8 *data, size_t size);

// Decode to GLB/DAE at OUT_PATH (format by extension), writing embedded TXE
// textures as sibling PNGs. ERR_NOTHING_TO_DO when not a Pikmin MOD.
enumError DecodePIKMOD (const u8 *data, uint size, ccp out_path);

// Encode MODEL to canonical Pikmin MOD bytes (see header note).
enumError EncodePIKMOD (const model_t *model, u8 **out, uint *out_size);

// Encode MODEL straight to a file.
enumError EncodeModelToPIKMOD (const model_t *model, ccp out_path);

// True when DATA looks like a TXE texture (dimensions + GX size match).
bool IsTXE (const u8 *data, size_t size);

// Decode a TXE to tightly packed RGBA8 (*DEST, WIDTH x HEIGHT).
enumError DecodeTXE_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size);

// Decode a TXE straight to a PNG file.
enumError DecodeTXE (const u8 *data, uint size, ccp out_path);

// Pikmin ARC/DIR pair: index (.dir) + data (.arc). Lists members; entries
// reference slices of ARC_DATA (which must outlive the list).
typedef struct
{
	char *name;
	const u8 *data;
	uint size;
} pikarc_entry_t;

enumError ScanPIKARC (pikarc_entry_t **entries, uint *n_entries, const u8 *dir_data, uint dir_size,
	const u8 *arc_data, uint arc_size);
void FreePIKARC (pikarc_entry_t *entries, uint n_entries);

// Rebuild a pair: DIR index into *DIR_OUT plus raw ARC blob into *ARC_OUT.
enumError CreatePIKARC (u8 **dir_out, uint *dir_size, u8 **arc_out, uint *arc_size,
	const pikarc_entry_t *entries, uint n_entries);

#endif // LIB_PIK1_H
