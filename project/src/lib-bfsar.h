#ifndef SZS_LIB_BFSAR_H
#define SZS_LIB_BFSAR_H 1

#include "lib-std.h"

// BFSAR / BCSAR -- Wii U / Switch / 3DS Sound Archive ("FSAR"/"CSAR" magic;
// the RSAR/BRSAR family's successor generation, a completely different byte
// format despite the similar purpose -- see lib-rbnk.h for the older,
// unrelated Wii RSAR/RBNK convention). Real spec pulled from the Citric
// Composer project (Gota7/Citric-Composer, docs/specs/common.md -- an
// actual written spec, not reverse engineered from scratch here) for the
// generic Reference/SizedReference/Table conventions, then the exact
// STRG (string table + binary trie name lookup) and INFO (the archive's
// real directory: Sound/Bank/Player/WaveArchive/SoundGroup/Group/File
// tables) block layouts confirmed byte-for-byte against a real retail
// Wii U file (Nintendo Land's content/Common/Sound/lunch.bfsar, 48 MB,
// 2323 real sound names all resolved correctly) rather than trusted from
// the C# source alone.
//
// Scope, deliberately: this parses the archive's real *directory* --
// every Sound/Bank/Player/WaveArchive/SoundGroup/Group entry's index, Id,
// and real name (via the STRG binary trie) -- but does not yet decode
// each entry's own internal fields (a sound entry's player/volume/detail
// reference into its stream/sequence/wave-data sub-structure, a bank's
// RBNK-shaped instrument tree, etc.). That is real further work on top of
// this, not a shortcut taken here: the directory alone is already useful
// (it's what answers "what's in this archive"), and every offset it
// reports is verified against real bytes.

typedef enum bfsar_sound_type_t
{
	BFSAR_TYPE_NONE = 0,
	BFSAR_TYPE_SOUND = 1,
	BFSAR_TYPE_SOUNDGROUP = 2,
	BFSAR_TYPE_BANK = 3,
	BFSAR_TYPE_PLAYER = 4,
	BFSAR_TYPE_WAVEARCHIVE = 5,
	BFSAR_TYPE_GROUP = 6,
} bfsar_sound_type_t;

typedef struct bfsar_entry_t
{
	u32 id; // 0xTTIIIIII (TT: bfsar_sound_type_t, IIIIII: index)
	bool present; // false: this slot's reference offset was NULL_PTR (-1)
	ccp name; // resolved via STRG's lookup trie, or NULL if not found
} bfsar_entry_t;

typedef struct bfsar_table_t
{
	bfsar_sound_type_t type;
	ccp type_name;
	uint n_entry;
	bfsar_entry_t *entry; // n_entry entries, malloc'd
} bfsar_table_t;

#define BFSAR_MAX_TABLE 7 // Sound, Bank, Player, WaveArchive, SoundGroup, Group, File

typedef struct bfsar_t
{
	bool is_ctr; // true: 'CSAR' (3DS), false: 'FSAR' (Wii U/Switch)
	bool little_endian;
	u8 version_major, version_minor, version_revision;
	uint n_table;
	bfsar_table_t table[BFSAR_MAX_TABLE];

	// owns the STRG string data referenced by every entry->name above
	char **strings;
	uint n_strings;
} bfsar_t;

enumError ScanBFSAR (bfsar_t *bfsar, const u8 *data, uint size);
void ResetBFSAR (bfsar_t *bfsar);

// Dump as a lossless-structure XML, same convention as wrbnk's DumpRBNK_XML().
enumError DumpBFSAR_XML (const bfsar_t *bfsar, FILE *f, ccp source_name);

#endif // SZS_LIB_BFSAR_H
