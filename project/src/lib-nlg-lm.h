// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Next Level Games LM2 / LM3 / Mario Strikers support, ported from the
// wire layouts in KillzXGaming/NextLevelLibrary (MIT, see CREDITS.md).
// Covers what lib-fedforce.c (Federation Force, LE) does not:
//
//  - LM2 / LM3 .dict/.data dictionary archives (BE identifier 0x5824F3A9):
//    structural LM2-vs-LM3 detection, block tables, zlib block decoding,
//    and the 12-byte chunk-table system (0x1301 FileHeader pairs, child
//    lists via Flags2/Flags3, per-chunk block selection).
//  - LM2 models (16-byte header / 0x28 mesh, hash-driven vertex layouts)
//    and LM3 models (12-byte header / 0x40 mesh, fixed vertex layout with
//    a separate skinning buffer) -> model_t, plus LM2/LM3 skeletons
//    (0x7101..0x7106) -> joints.
//  - LM2 textures (CTR PICA, transpose swizzle) and LM3 textures (Switch
//    RGBA8/BCn/ASTC, block-linear) -> RGBA8.
//  - Animation 0x7000 tracks (opcode-driven keys) and script 0x5000/0x6500
//    tables -> human-readable text dumps.
//  - NLG hash-name reverse table (generated from the upstream Hashes/
//    wordlists, see scripts/gen_nlg_names.py) shared by all NLG code.
//  - Mario Strikers SANIM animation streams -> text dumps.
//
// LM2/LM3 models, skeletons and textures ride the same FEDM/FEDS/FEDT
// container shape lib-fedforce.c defines, with version 2 (LM2) and 3 (LM3)
// alongside its version 1 (Federation Force). They are extractor
// intermediates, NOT retail formats. Retail RLG (.rlg) needs no change:
// lib-glg.c already exceeds what NextLevelLibrary parses (its RLG_Parser
// reads section headers and discards the payloads; it defines no bone
// layouts at all).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_NLG_LM_H
#define SZS_LIB_NLG_LM_H 1

#include "types.h"
#include "lib-nlg-probe.h"

#ifdef __cplusplus
extern "C"
{
#endif

#include "lib-model-glb.h"

	// NLG game variant carried inside one .dict/.data pair.
	typedef enum
	{
		NLG_UNKNOWN = 0,
		NLG_FEDFORCE = 1, // Metroid Prime: Federation Force (LE)
		NLG_LM2 = 2, // Luigi's Mansion: Dark Moon (BE)
		NLG_LM3 = 3, // Luigi's Mansion 3 (BE)
	} nlg_variant_t;

	// The upstream DetectVersion rule (BE u32 @12 == 0x78340300) is a single
	// retail file's block counts, not a magic; detect structurally instead.
	// Returns NLG_UNKNOWN when neither the LM2 nor the LM3 header validates.
	nlg_variant_t NLGDetectVariant (const u8 *dict, uint dict_size);

	// Reverse lookup over the upstream hash wordlists. Returns the known name,
	// or NULL when unknown (OUT gets "%X" hex then, like FedForceHashName).
	ccp NLGHashName (u32 hash, char out[16]);

	// One decoded dictionary block-table entry (LM2 and LM3 share the 16-byte
	// offset/decomp/comp/flags record shape; only the header around it differs).
	typedef struct nlg_block_t
	{
		u32 offset, decomp_size, comp_size, flags;
		u8 source_index; // (flags>>16)&0xFF: external source selector
		u8 source_type; // 0=unknown, 1=TABLE, 2=DATA (from flags, upstream rule)
	} nlg_block_t;

	// Structured LM2/LM3 header. Returns ERR_OK only when every count fits and
	// every string is NUL-terminated inside DICT_SIZE.
	enumError ScanLM2Dict (const u8 *dict, uint dict_size, nlg_block_t **blocks, uint *n_blocks,
		const char ***strings, uint *n_strings, bool *compressed);
	enumError ScanLM3Dict (const u8 *dict, uint dict_size, nlg_block_t **blocks, uint *n_blocks,
		const char ***strings, uint *n_strings, bool *compressed);
	void FreeNLGDict (nlg_block_t *blocks, const char **strings, uint n_strings);

	// One parsed 12-byte chunk-table entry. LEAF entries address a slice of one
	// decompressed buffer; PARENT entries address child indices in the table.
	typedef struct nlg_chunk_t
	{
		u16 type;
		u16 flags;
		u32 size; // child count (parent) or data size (leaf)
		u32 offset; // child index (parent) or data offset (leaf)
		u8 is_file; // type was FileHeader 0x1301: pairs with the next entry
		u32 hash_type; // file-entry magic (from the file-table buffer)
		u32 path_hash; // file-entry path hash (from the file-table buffer)
	} nlg_chunk_t;

	enumError ScanNLGChunks (nlg_chunk_t **chunks, uint *n, const u8 *data, uint size);
	static inline bool NLGChunkHasChildren (u16 flags)
	{
		return (flags >> 12) > 2;
	}
	static inline uint NLGChunkBlockIndex (u16 flags)
	{
		return (flags >> 12) & 7;
	}

	// Typed extraction over a .dict/.data pair: block-level dumps are left to
	// ExtractNLGDictArchive; this exports models (-> .fedmodel + .glb),
	// textures (-> .fedtex + .png), skeletons (-> .fedskel), animations and
	// scripts (-> .txt), and hash-resolved raw dumps for anything else.
	// Best effort: structural surprises end the pass quietly.
	enumError ExtractNLGTyped (ccp dest, const u8 *dict, uint dict_size, const u8 *data_raw,
		size_t data_raw_size, nlg_variant_t variant, bool is_compressed);

	// Known texture path hashes, for material diffuse resolution.
	typedef struct
	{
		const u32 *hash;
		uint n;
	} nlg_texset_t;

	// Standalone FEDM/FEDS/FEDT containers, versions 1 (Federation Force),
	// 2 (LM2) and 3 (LM3). Version 1 delegates to lib-fedforce.c. TS may be
	// NULL (standalone decode: materials keep names but bind no PNG).
	model_t *ParseNLGModel (const u8 *data, size_t size, const nlg_texset_t *ts);
	model_t *ParseNLGSkeleton (const u8 *data, size_t size);
	enumError DecodeNLGTexture (u8 **dest, uint *width, uint *height, const u8 *data, size_t size);

	// Mario Strikers SANIM animation stream: flags u16, magic u16, size u32
	// chunks (BE), AnimStart 0x7000 ... rotation 0x7101 / translation 0x7102 /
	// indexed 0x7103 keys.
	enumError ExtractSANIMArchive (ccp arg, ccp basedir, uint depth);

#ifdef __cplusplus
}
#endif

#endif
