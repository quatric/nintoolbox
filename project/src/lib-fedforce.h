// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Metroid Prime: Federation Force (3DS, Next Level Games) support.
//
// Retail layout per KillzXGaming/Metroid-Fed-Force-Dumper (MIT, re-implemented
// here, not copied): a LE .dict/.data pair. The .dict holds a block table
// (offset/decomp/comp/flags u32 x4 per block) plus one file-table reference
// (name hash u32, file-section count u16, file count u16, 8 block indices
// for FedForce vs 16 for LM3) and a string list of external extensions.
// The .data holds a 12-byte-entry chunk table (u16 type, u16 flags, u32 size,
// u32 offset; FileHeader 0x1301 entries come in header+body pairs) whose
// leaf data lives in the block buffers. File entries (header type 0x1301)
// carry a hash-type + file-path hash in their 8-byte header; typed files of
// interest here are Model 0xB000, Texture 0xB500, Skeleton 0x7100,
// Font 0x7010 and MessageData 0x7020 with the sub-chunks listed below.
//
// This module implements:
//  - NLG hash (h*33+c, RoadrunnerWMC gist as used by the dumper) + a small
//    built-in reverse table (material presets driving vertex layouts,
//    languages, and fixed hashes from the dumper's Hashing.cs).
//  - Chunk-table scan over a decompressed .data buffer.
//  - Model parse (B000 family) -> model_t with the dumper's 47 material
//    presets and stride table, plus skeleton joints when 7101..7105 chunks
//    are present in the same container.
//  - Skeleton-only and texture-only scans (7100 / B500 families).
//  - NLOC localization (both BE "NLOC" and LE no-magic version2 forms).
//  - NLG font description text (Font/Glyph/Kern lines).
//  - Self-describing FEDM/FEDS/FEDT containers (magics "FEDM"/"FEDS"/"FEDT")
//    used for extracted chunk files and synthetic fixtures; retail .dict
//    parsing emits these, and wmdlt/wimgt decode them. They are NOT retail
//    formats.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_FEDFORCE_H
#define SZS_LIB_FEDFORCE_H 1

#include "types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#include "lib-model-glb.h"

	// Chunk types of interest (full enum lives in the dumper's Enums.cs).
	enum
	{
		FED_CHUNK_FILEHEADER = 0x1301,
		FED_CHUNK_MODEL = 0xB000,
		FED_CHUNK_TEXTURE = 0xB500,
		FED_CHUNK_SKELETON = 0x7100,
		FED_CHUNK_FONT = 0x7010,
		FED_CHUNK_MESSAGEDATA = 0x7020,
		FED_CHUNK_MODEL_TRANSFORM = 0xB001,
		FED_CHUNK_MODEL_INFO = 0xB002,
		FED_CHUNK_MESH_INFO = 0xB003,
		FED_CHUNK_VERT_START = 0xB004,
		FED_CHUNK_MESH_BUFFERS = 0xB005,
		FED_CHUNK_MATERIAL_DATA = 0xB006,
		FED_CHUNK_MATERIAL_LUT = 0xB007,
		FED_CHUNK_BOUND_RADIUS = 0xB008,
		FED_CHUNK_BOUND_BOX = 0xB009,
		FED_CHUNK_TEX_HEADER = 0xB501,
		FED_CHUNK_TEX_DATA = 0xB502,
		FED_CHUNK_SKEL_HEADER = 0x7101,
		FED_CHUNK_SKEL_BONEINFO = 0x7102,
		FED_CHUNK_SKEL_BONEXTM = 0x7103,
		FED_CHUNK_SKEL_BONEIDX = 0x7104,
		FED_CHUNK_SKEL_BONEHASH = 0x7105,
		FED_CHUNK_SKIN_START = 0xB100,
	};

	// One parsed 12-byte chunk-table entry.
	typedef struct fed_chunk_t
	{
		u16 type;
		u16 flags;
		u32 size; // child count when HasChildren, else data size
		u32 offset; // child index when HasChildren, else data offset
		u8 is_file; // header type was FileHeader: this entry pairs with next
		u32 hash_type; // valid when is_file (from header data)
		u32 path_hash; // valid when is_file (from header data)
	} fed_chunk_t;

	// NLG hash used by all Next Level Games FedForce/LM files.
	u32 FedForceHash (const char *name, bool case_sensitive);
	// Reverse lookup for known hashes (materials, languages, fixed names).
	// Returns NULL when unknown; out buffer gets "XXXXXXXX" hex otherwise.
	ccp FedForceHashName (u32 hash, char out[16]);

	// Chunk flag helpers (FedForce uses the LM2 3-bit block-index variant).
	bool FedChunkHasChildren (u16 flags);
	uint FedChunkBlockIndex (u16 flags);
	uint FedChunkAlignment (u16 flags);

	// Scan a decompressed .data chunk-table buffer.
	// Returns ERR_OK with *CHUNKS (CALLOC'd, *N entries) on success.
	enumError ScanFedForceChunks (fed_chunk_t **chunks, uint *n, const u8 *data, uint size);

	// FedForce .dict header scan (LE; handles both BE/LE identifier forms).
	// Fills block table + file-table reference + string list views into DICT.
	typedef struct fed_dict_block_t
	{
		u32 offset, decomp_size, comp_size, flags;
		u8 source_index;
	} fed_dict_block_t;

	typedef struct fed_dict_ref_t
	{
		u32 name_hash;
		u16 file_section_count, file_count;
		u8 block_indices[8];
	} fed_dict_ref_t;

	enumError ScanFedForceDict (const u8 *dict, uint dict_size, bool *is_fed,
		fed_dict_block_t **blocks, uint *n_blocks, fed_dict_ref_t *ref, const char ***strings,
		uint *n_strings);
	void FreeFedForceDict (fed_dict_block_t *blocks, const char **strings, uint n_strings);

	// Standalone containers emitted by the extractor (NOT retail).
	bool IsFedForceModel (const u8 *data, size_t size); // "FEDM"
	bool IsFedForceSkeleton (const u8 *data, size_t size); // "FEDS"
	bool IsFedForceTexture (const u8 *data, size_t size); // "FEDT"
	model_t *ParseFedForceModel (const u8 *data, size_t size);
	model_t *ParseFedForceSkeleton (const u8 *data, size_t size);
	// Decode FEDT pixels to tightly packed RGBA8.
	enumError DecodeFedForceTexture (
		u8 **dest, uint *width, uint *height, const u8 *data, size_t size);
	// Build FEDM/FEDS/FEDT containers from raw chunk payloads (for the
	// extractor and for tests).
	enumError BuildFedForceModel (u8 **dest, uint *dest_size, const u8 *b008, uint b008_size,
		const u8 *b009, uint b009_size, const u8 *b001, uint b001_size, const u8 *b003,
		uint b003_size, const u8 *b004, uint b004_size, const u8 *b005, uint b005_size,
		const u8 *b006, uint b006_size, const u8 *b007, uint b007_size, const u8 *b002,
		uint b002_size, const u8 *s101, uint s101_size, const u8 *s102, uint s102_size,
		const u8 *s103, uint s103_size, const u8 *s104, uint s104_size, const u8 *s105,
		uint s105_size);
	enumError BuildFedForceTexture (u8 **dest, uint *dest_size, uint width, uint height,
		uint pica_format, const u8 *pixels, uint pixels_size, uint tex_hash);

	// NLOC localization (BE "NLOC" + LE no-magic version2).
	bool IsFedForceNLOC (const u8 *data, size_t size);
	typedef struct fed_nloc_msg_t
	{
		u32 id;
		char *text; // owned UTF-8
	} fed_nloc_msg_t;
	enumError ScanFedForceNLOC (
		const u8 *data, size_t size, fed_nloc_msg_t **msgs, uint *n_msgs, u32 *language_id);
	void FreeFedForceNLOC (fed_nloc_msg_t *msgs, uint n);
	// Render messages as "XXXXXXXX label\ntext\n\n..." text; parse it back.
	enumError FedForceNLOCToText (u8 **dest, uint *dest_size, const fed_nloc_msg_t *msgs, uint n);
	enumError FedForceTextToNLOC (u8 **dest, uint *dest_size, const u8 *text, uint text_size,
		u32 language_id, bool big_endian);

	// NLG font description text (Font .../Glyph .../Kern .../END lines).
	bool IsFedForceFont (const u8 *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
