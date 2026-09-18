// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Wario World (GameCube) resource container (.rsc) and embedded models.
//
// Re-implemented in C from KillzXGaming/MdlConverter's GCNLibrary/WarioWorld
// (MIT-licensed, tool by KillzXGaming). Layout recovered from RSC_Parser,
// Model/ModelGroup/Shape/Packet and the resource-type table, re-expressed
// against this project's own model_t/GLB pipeline. No upstream code is
// copied.
//
// An RSC file is 0x20 bytes of unknown header words followed by a linked
// list of resources: {u32 type, u32 size, u32 nextOff, 20 reserved bytes}
// plus size bytes of payload, 32-aligned. Resource types select the member
// extension (.tpl, .ww_static_model, .ww_rigged_model, .ww_map_model,
// .ww_skel_animation, .ww_animation, .ww_collison, .ww_phys_collison,
// .ww_lights, .ww_map_placements, .ww_spawn_placements, .ww_message,
// or _<type>.bin for unknown ids).
//
// Static/rigged/map models share one header of 12 u32s (absolute offsets
// stored divided by 4): positions, normals, colours, texcoords,
// material-colours, texture-container, then (count, offset) pairs for draw
// elements, shapes and packets. Vertices are S16 indices into the pools
// (positions raw, normals /32767, colours RGBA4, texcoords /1024) in
// standard GX display lists. Per-packet Flags2 selects the attribute
// layout; Flags2 16/17/19 (shifted opcodes in the reference reader) are
// rejected cleanly. Geometry decodes unskinned: the reference bakes
// skeletons from separate animation resources, which v1 preserves
// byte-exact inside the RSC without converting.
//-----------------------------------------------------------------------------
#ifndef LIB_WWRSC_H
#define LIB_WWRSC_H 1

#include "types.h"
#include "lib-model-glb.h"

// Wario World resource types (see RSC_Parser.ResourceType).
typedef enum
{
	WW_STATIC_MODEL = 0,
	WW_TEXTURE_CONTAINER = 1,
	WW_SKEL_ANIMATION = 2,
	WW_MAP_MODEL = 3,
	WW_COLLISION = 4,
	WW_RIGGED_MODEL = 5,
	WW_MAP_PARAMS = 6,
	WW_LIGHTING = 8,
	WW_SPAWNS = 9,
	WW_ANIMATION = 10,
	WW_SPECIAL_COLLISION = 11,
	WW_MESSAGE = 14,
} ww_rtype_t;

// One RSC member. DATA points into the source buffer (not owned).
typedef struct
{
	ww_rtype_t type;
	char name[64]; // e.g. Root_File3.ww_static_model
	const u8 *data;
	uint size;
} wwrsc_entry_t;

// Full structural walk. Unknowns (0x20 header) are preserved for rebuild.
enumError ScanWWRSC (wwrsc_entry_t **entries, uint *n_entries, u8 unknowns[32],
	const u8 *data, uint size);

// Rebuild a byte-exact RSC (same member order, 32-aligned).
enumError CreateWWRSC (u8 **dest, uint *dest_size, const u8 unknowns[32],
	const wwrsc_entry_t *entries, uint n_entries);

void FreeWWRSC (wwrsc_entry_t *entries, uint n_entries);

// Member file extension for a resource type (".tpl", ".ww_static_model", ...).
ccp WWRExtension (ww_rtype_t type);

// True when DATA looks like a WW static/rigged/map model.
bool IsWWModel (const u8 *data, size_t size);

// Parse to a heap model_t (free with FreeModel). Unskinned static geometry
// with material colours and texture references; NULL on invalid input.
model_t *ParseWWModel (const u8 *data, size_t size);

// Decode to GLB/DAE at OUT_PATH (format by extension). The embedded TPL
// container (if any) is carved out beside it as <base>.tpl; material
// texture names keep working against it. ERR_NOTHING_TO_DO when no model.
enumError DecodeWWModel (const u8 *data, uint size, ccp out_path);

// Encode MODEL to a canonical WW static-model stream (Flags2=3 layout:
// positions, normals, colours, one UV, no matrix slots). Texture pixels
// are referenced by name only; the TPL container is not rebuilt (same
// documented limitation as the HSF/BNFM encoders).
enumError EncodeWWModel (const model_t *model, u8 **out, uint *out_size);

// Encode MODEL straight to a file.
enumError EncodeModelToWWModel (const model_t *model, ccp out_path);

#endif // LIB_WWRSC_H
