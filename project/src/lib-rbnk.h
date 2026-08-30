#ifndef SZS_LIB_RBNK_H
#define SZS_LIB_RBNK_H 1

#include "lib-std.h"

// RBNK -- Nintendo's Wii instrument bank container (what an RSEQ sequence's
// program-change events select into): a "Data" section holding, per MIDI
// program number, a note-lookup tree (RangeTable: a sparse list of
// {key,child} pairs; IndexTable: a dense Min..Max array) bottoming out at
// InstParam entries (which wave to play, ADSR envelope, pitch/volume/pan),
// plus (RBNK version < 2 only) a "Wave" section of WaveInfo entries -- the
// same struct RWAV's own INFO chunk carries, just without
// the RWAV wrapper. Version >= 2 banks reference an embedded RWAR archive
// instead of a direct Wave section; not handled here (no real sample seen
// yet to verify the RWAR-embedding convention against -- see lib-rbnk.c).
//
// Field layout ported from BrawlLib (soopercool101/BrawlCrate)
// SSBB/Types/Audio/RBNK.cs + SSBB/ResourceNodes/RSAR/File Types/RBNK/*.cs
// (RBNKDataGroupNode/RBNKDataRangeTableNode/RBNKDataIndexTableNode's actual
// OnPopulate() tree-walk logic, not just the raw struct shapes -- the
// struct alone doesn't show that every ruint offset in the Data section,
// at any nesting depth, is relative to the *top* of the Data RuintList,
// not to each intermediate table's own address).

typedef enum rbnk_entry_type_t
{
	RBNK_ENTRY_INVALID = 0,
	RBNK_ENTRY_INST = 1,
	RBNK_ENTRY_RANGE = 2,
	RBNK_ENTRY_INDEX = 3,
	RBNK_ENTRY_NULL = 4,
} rbnk_entry_type_t;

typedef struct rbnk_inst_t
{
	u32 wave_index;
	u8 attack, decay, sustain, release, hold;
	u8 wave_data_location_type;
	u8 note_off_type;
	u8 alternate_assign;
	u8 original_key;
	u8 volume, pan, surround_pan;
	float pitch;
} rbnk_inst_t;

// A tree node in one program's note-lookup tree. RANGE nodes carry
// N_CHILD sparse {key,child} pairs (KEYS[i] -> CHILDREN[i]); INDEX nodes
// carry a dense Min..Max run (KEY_MIN + i -> CHILDREN[i], i=0..N_CHILD-1,
// KEYS is NULL). INST/NULL/INVALID are leaves (N_CHILD == 0).
typedef struct rbnk_node_t
{
	rbnk_entry_type_t type;
	u8 key_min; // INDEX: the table's Min; RANGE/leaf: unused
	rbnk_inst_t inst; // valid iff type == RBNK_ENTRY_INST
	uint n_child;
	u8 *keys; // RANGE only: n_child bytes, malloc'd
	struct rbnk_node_t *child; // n_child entries, malloc'd
} rbnk_node_t;

typedef struct rbnk_wave_t
{
	u8 encoding; // 0: PCM8, 1: PCM16, 2: ADPCM_THP
	u8 looped;
	u8 channels;
	u16 sample_rate;
	s64 n_samples;
	s64 loop_start;
} rbnk_wave_t;

typedef struct rbnk_t
{
	u16 version_major, version_minor;
	uint n_program;
	rbnk_node_t *program; // n_program entries, one tree root per program number, malloc'd
	uint n_wave;
	rbnk_wave_t
		*wave; // n_wave entries, malloc'd; NULL if this is a version>=2 (RWAR-embedded) bank
} rbnk_t;

// Parse an RBNK binary (DATA/size must start at the "RBNK" tag).
enumError ScanRBNK (rbnk_t *rbnk, const u8 *data, uint size);
void ResetRBNK (rbnk_t *rbnk);

// Dump RBNK as a lossless-structure XML (same "structure we can't yet fully
// round-trip, but can at least see and diff" convention this project uses
// for e.g. the BFRES Switch manifest -- see wszst.c's
// extract_bfres_switch_manifest()).
enumError DumpRBNK_XML (const rbnk_t *rbnk, FILE *f, ccp source_name);

// Parse lossless-structure XML into an rbnk_t structure.
enumError ParseRBNK_XML (rbnk_t *rbnk, const char *xml_str, size_t xml_len);

// Encode an rbnk_t structure into a valid NW4R RBNK binary buffer.
enumError EncodeRBNK (const rbnk_t *rbnk, u8 **out_data, uint *out_size);

#endif // SZS_LIB_RBNK_H
