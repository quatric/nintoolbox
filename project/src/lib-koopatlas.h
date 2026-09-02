#ifndef SZS_LIB_KOOPATLAS_H
#define SZS_LIB_KOOPATLAS_H 1

#include "types.h"
#include "file-type.h"

//
///////////////////////////////////////////////////////////////////////////////
/////////    Koopatlas Binary World Map (.kpbin / KP_m) Format       //////////
///////////////////////////////////////////////////////////////////////////////
//
//  Koopatlas is the 2D world map editor used in Newer Super Mario Bros. Wii
//  and many NSMBW mods.  Maps are edited in JSON (.kpmap) and compiled into
//  an optimized big-endian binary format (.kpbin) loaded by the game engine.
//
//  Header (0x2C / 44 bytes):
//    u32 magic        – 0x4B505F6D ("KP_m")
//    s32 version      – typically 2
//    s32 layer_count  – number of map layers
//    u32 layer_offs   – file offset to array of layer pointers
//    s32 tileset_count– number of GXTexObj tileset headers
//    u32 tileset_offs – file offset to tileset headers
//    u32 unlock_offs  – file offset to unlock bytecode
//    u32 sector_offs  – file offset to packed 16x16 sector definitions
//    u32 bg_name_offs – file offset to background image name string
//    u32 world_offs   – file offset to world definition list
//    s32 world_count  – number of world definitions
//
///////////////////////////////////////////////////////////////////////////////

#define KPBIN_MAGIC "KP_m"
#define KPBIN_MAGIC_NUM 0x4B505F6D

// Node types
enum kp_node_type_e
{
	KP_NODE_PASSTHROUGH  = 0,
	KP_NODE_STOP         = 1,
	KP_NODE_LEVEL        = 2,
	KP_NODE_CHANGE       = 3,
	KP_NODE_WORLD_CHANGE = 4
};

// Layer types
enum kp_layer_type_e
{
	KP_LAYER_OBJECTS = 0,
	KP_LAYER_DOODADS = 1,
	KP_LAYER_PATHS   = 2
};

// Animation types for doodads
enum kp_anim_type_e
{
	KP_ANIM_X_POS   = 0,
	KP_ANIM_Y_POS   = 1,
	KP_ANIM_ANGLE   = 2,
	KP_ANIM_X_SCALE = 3,
	KP_ANIM_Y_SCALE = 4,
	KP_ANIM_OPACITY = 5
};

typedef struct kp_doodad_anim_t
{
	u32 loop;
	u32 curve;
	u32 frame_count;
	u32 type;
	u32 start;
	u32 end;
	u32 delay;
	u32 delay_offset;
} kp_doodad_anim_t;

typedef struct kp_doodad_t
{
	float x, y;
	float w, h;
	float angle;
	u32 tex_offs;
	uint n_anims;
	kp_doodad_anim_t *anims;
} kp_doodad_t;

typedef struct kp_node_t
{
	s16 x, y;
	int exit_paths[4]; // indices into path array (0=left, 1=right, 2=up, 3=down, -1=none)
	u32 tile_layer_off;
	u32 dood_layer_off;
	u8 type;
	u8 level_world;    // if type == KP_NODE_LEVEL
	u8 level_num;
	bool has_secret;
	char *dest_map;    // if type == KP_NODE_CHANGE
	u8 this_id;
	u8 foreign_id;
	u8 transition;
	u8 world_id;       // if type == KP_NODE_WORLD_CHANGE
} kp_node_t;

typedef struct kp_path_t
{
	int start_node;    // index into node array (-1 if unresolvable)
	int end_node;      // index into node array (-1 if unresolvable)
	u32 tile_layer_off;
	u32 dood_layer_off;
	u8 is_available;
	u8 is_secret;
	float speed;
	u32 animation;
} kp_path_t;

typedef struct kp_layer_t
{
	u32 type;          // KP_LAYER_OBJECTS, DOODADS, or PATHS
	u8 alpha;
	char *tileset_name;
	s32 sector_bounds[4];
	s32 real_bounds[4];
	uint n_tile_indices;
	u16 *tile_indices;

	// If DOODADS
	uint n_doodads;
	kp_doodad_t *doodads;

	// If PATHS
	uint n_nodes;
	kp_node_t *nodes;
	uint n_paths;
	kp_path_t *paths;
} kp_layer_t;

typedef struct kp_world_t
{
	char *name;
	u8 world_id;
	u8 unique_key;
	u8 music_track_id;
	u8 title_world;
	u8 title_level;
	u32 fs_text_color[2];
	u32 fs_hint_color[2];
	u32 hud_text_color[2];
} kp_world_t;

typedef struct kpbin_t
{
	s32 version;
	char *bg_name;
	uint n_layers;
	kp_layer_t *layers;
	uint n_worlds;
	kp_world_t *worlds;
	uint n_tilesets;
	u32 sector_offs;
	u32 unlock_offs;
} kpbin_t;

// Returns true if 'data' is a valid Koopatlas binary map (.kpbin)
bool IsKPBin (const u8 *data, size_t size);

// Parses .kpbin data into the in-memory representation
enumError ScanKPBin (kpbin_t *kp, const u8 *data, size_t size);

// Frees all allocations inside kp
void ResetKPBin (kpbin_t *kp);

// Dumps human-readable representation of a Koopatlas map
enumError DumpKPBinText (const kpbin_t *kp, char **out, size_t *out_size);

// Dumps structured JSON representation of a Koopatlas map
enumError DumpKPBinJson (const kpbin_t *kp, char **out, size_t *out_size);

#endif // SZS_LIB_KOOPATLAS_H
