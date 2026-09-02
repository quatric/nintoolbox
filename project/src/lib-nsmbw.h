#ifndef SZS_LIB_NSMBW_H
#define SZS_LIB_NSMBW_H 1

#include "types.h"
#include "lib-nintendo.h"

//
///////////////////////////////////////////////////////////////////////////////
/////////    NSMBW Tileset Format (BG_tex / BG_chk / BG_unt arcs)   //////////
///////////////////////////////////////////////////////////////////////////////
//
//  An NSMBW / Newer Super Mario Bros. Wii tileset is a standard U8 (.arc)
//  archive with the following internal layout:
//
//    BG_tex/<name>_tex.bin.LZ   – LZ11-compressed RGB4A3/RGB555 GX texture
//                                  1024 × 256 pixels, GX block-4 texel layout
//                                  32 × 8 texels of 32 × 32 pixels each
//                                  Each texel holds one 24 × 24 tile with 4px
//                                  clamped borders (outer rows/cols duplicated)
//    BG_chk/d_bgchk_<name>.bin  – 256 tiles × 8 bytes behaviour flags
//    BG_unt/<name>.bin           – variable-length object-definition strings
//    BG_unt/<name>_hd.bin        – object metadata: per-object u16 offset +
//                                  u8 width + u8 height (4 bytes each)
//
//  We do NOT re-implement the U8 archive layer (already supported by wszst).
//  This module decodes the _tex.bin.LZ (RGB4A3 → flat ARGB8), the behaviour
//  table (8 bytes / tile), and the object definitions, producing:
//    <name>_tex.png  –  ARGB8 PNG of the full 1024 × 256 texture
//    <name>_chk.txt  –  human-readable behaviour flags
//    <name>_unt.txt  –  human-readable object definitions
//
///////////////////////////////////////////////////////////////////////////////

// Tile behaviour: 8 bytes stored big-endian
typedef struct nsmbw_tile_beh_t
{
	u8 byte[8];
} nsmbw_tile_beh_t;

// One tile row in an object: (flags, tileset_tile, tileset_slot)
typedef struct nsmbw_obj_tile_t
{
	u8 flags;      // combination flags (special rendering)
	u8 tile;       // tile index within the tileset texture (0-255)
	u8 slot;       // Pa0-Pa3 slot (0-3)
} nsmbw_obj_tile_t;

// One row of tiles within an object definition
typedef struct nsmbw_obj_row_t
{
	uint n_tiles;
	nsmbw_obj_tile_t *tiles;
} nsmbw_obj_row_t;

// A full object definition
typedef struct nsmbw_obj_t
{
	uint width;
	uint height;
	uint upper_slope;  // 0 = none
	uint lower_slope;  // 0 = none
	uint n_rows;
	nsmbw_obj_row_t *rows;
} nsmbw_obj_t;

// Decoded NSMBW tileset
typedef struct nsmbw_tileset_t
{
	// raw decoded (LZ11 decompressed) RGB4A3 texture bytes (524288 bytes for 1024×256)
	u8 *tex_raw;
	uint tex_raw_size;

	// ARGB8 flat pixel data (1024 × 256 × 4 bytes = 1048576 bytes)
	u8 *argb;

	// Behaviour table: 256 entries × 8 bytes
	nsmbw_tile_beh_t beh[256];
	bool have_beh;

	// Object definitions
	uint n_objects;
	nsmbw_obj_t *objects;
} nsmbw_tileset_t;

// Detect whether an extracted arc file-set looks like an NSMBW tileset.
// Pass the internal paths present in the arc.  Returns true if it matches.
bool IsNSMBWTilesetArc (const char *const *paths, uint n_paths);

// Decode RGB4A3/RGB555 GX texel-tiled data into flat ARGB8.
// src must be tex_size bytes (typically 524288 = 1024*256*2).
// dst must be 1024*256*4 bytes (ARGB8, row-major).
void DecodeRGB4A3 (u8 *dst, const u8 *src, uint src_size);

// Encode flat ARGB8 (1024×256×4) back to RGB4A3 GX texel-tiled.
// dst must be at least 524288 bytes.
void EncodeRGB4A3 (u8 *dst, const u8 *src);

// Scan the three component blobs of an NSMBW tileset.
//   tex_lz:  raw bytes of the _tex.bin.LZ file (LZ11-compressed)
//   chk:     raw bytes of the d_bgchk_*.bin file (may be NULL)
//   unt:     raw bytes of the *.bin object-def file (may be NULL)
//   unt_hd:  raw bytes of the *_hd.bin metadata file (may be NULL)
enumError ScanNSMBWTileset (nsmbw_tileset_t *ts,
	const u8 *tex_lz, uint tex_lz_size,
	const u8 *chk, uint chk_size,
	const u8 *unt, uint unt_size,
	const u8 *unt_hd, uint unt_hd_size);

// Free all allocations inside ts.
void ResetNSMBWTileset (nsmbw_tileset_t *ts);

// Dump behaviour table to human-readable text.
enumError DumpNSMBWBehaviour (const nsmbw_tileset_t *ts,
	char **out, size_t *out_size, ccp tileset_name);

// Dump object definitions to human-readable text.
enumError DumpNSMBWObjects (const nsmbw_tileset_t *ts,
	char **out, size_t *out_size, ccp tileset_name);

// Write the decoded texture as a raw ARGB8 .bin file (for use with wimgt).
// Returns an ALLOC'd buffer with 1024*256*4 bytes ARGB8 data.
enumError ExportNSMBWTexARGB (const nsmbw_tileset_t *ts,
	u8 **out, size_t *out_size);

// Re-encode the tileset (ARGB8 tex + beh table + object defs) and return the
// packed _tex.bin.LZ buffer.  The caller is responsible for repacking the arc.
enumError EncodeNSMBWTexLZ (const nsmbw_tileset_t *ts,
	u8 **out, uint *out_size);

// High-level helper: detect, decode, and write output files for an NSMBW
// tileset arc that has already been extracted to a directory.
//   arc_path: path to the .arc file
//   dest_dir: directory to write output files into (created if needed)
enumError ExtractNSMBWTilesetArc (ccp arc_path, ccp dest_dir);

//
///////////////////////////////////////////////////////////////////////////////
/////////    Newer SMBW LevelInfo.bin (NWRp) format                  //////////
///////////////////////////////////////////////////////////////////////////////

#define NWRP_MAGIC "NWRp"
#define NWRP_MAGIC_NUM 0x4E575270

typedef struct nwr_level_entry_t
{
	u8 file_world;       // 1-indexed (in file 0-indexed)
	u8 file_level;       // 1-indexed (in file 0-indexed)
	u8 display_world;
	u8 display_level;    // >= 100 is world header (100 = left, 101 = right)
	u16 flags;           // 0x0002 star coins, 0x0010 normal exit, 0x0020 secret exit, 0x0400 right side
	char *name;          // level / world name
} nwr_level_entry_t;

typedef struct nwr_world_t
{
	uint world_number;
	bool has_left;
	bool has_right;
	char *name_left;
	char *name_right;
	uint n_levels;
	nwr_level_entry_t *levels;
} nwr_world_t;

typedef struct nwr_levelinfo_t
{
	uint n_worlds;
	nwr_world_t *worlds;
	char *comments;
} nwr_levelinfo_t;

bool IsNWRLevelInfo (const u8 *data, size_t size);
enumError ScanNWRLevelInfo (nwr_levelinfo_t *li, const u8 *data, size_t size);
void ResetNWRLevelInfo (nwr_levelinfo_t *li);
enumError DumpNWRLevelInfoText (const nwr_levelinfo_t *li, char **out, size_t *out_size);
enumError CreateNWRLevelInfo (u8 **dest, size_t *dest_size, const nwr_levelinfo_t *li);

//
///////////////////////////////////////////////////////////////////////////////
/////////    Newer SMBW AnimTiles.bin (NWRa) format                  //////////
///////////////////////////////////////////////////////////////////////////////

#define NWRA_MAGIC "NWRa"
#define NWRA_MAGIC_NUM 0x4E575261

typedef struct nwr_animtile_entry_t
{
	char *tex_name;       // e.g. "water_fall_tex.bin"
	char *frame_delays;   // e.g. delay string or sequence
	u16 tile_num;         // tile ID
	u8 tileset_num;       // Pa0-Pa3 (0-3)
	u8 reverse;           // 0 or 1
} nwr_animtile_entry_t;

typedef struct nwr_animtiles_t
{
	uint n_entries;
	nwr_animtile_entry_t *entries;
} nwr_animtiles_t;

bool IsNWRAnimTiles (const u8 *data, size_t size);
enumError ScanNWRAnimTiles (nwr_animtiles_t *at, const u8 *data, size_t size);
void ResetNWRAnimTiles (nwr_animtiles_t *at);
enumError DumpNWRAnimTilesText (const nwr_animtiles_t *at, char **out, size_t *out_size);
enumError CreateNWRAnimTiles (u8 **dest, size_t *dest_size, const nwr_animtiles_t *at);

// Standalone collision check detection (2048 bytes)
bool IsNSMBWChk (const u8 *data, size_t size);

#endif // SZS_LIB_NSMBW_H
