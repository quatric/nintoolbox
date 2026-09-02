#ifndef SZS_LIB_UE4PAK_H
#define SZS_LIB_UE4PAK_H 1

#include "types.h"
#include "file-type.h"

// Unreal Engine 4 .pak container format (used by Mario & Luigi: Brothership,
// Yoshi's Crafted World, Shin Megami Tensei V, and other UE4 Switch titles).

#define UE4_PAK_MAGIC 0x5A6F12E1u

typedef struct ue4_pak_block_t
{
	u64 comp_start;
	u64 comp_end;
} ue4_pak_block_t;

typedef struct ue4_pak_entry_t
{
	char *filename; // allocated relative path
	u64 offset;
	u64 size;
	u64 uncompressed_size;
	uint compression_method;
	char method_name[32];
	u8 hash[20];
	bool encrypted;
	uint block_size;
	uint block_count;
	ue4_pak_block_t *blocks; // allocated array if compressed
} ue4_pak_entry_t;

typedef struct ue4_pak_t
{
	const u8 *data;
	size_t size;
	uint version;
	u64 index_offset;
	u64 index_size;
	bool encrypted_index;
	char mount_point[PATH_MAX];
	uint n_entries;
	ue4_pak_entry_t *entries;
	char comp_methods[5][32];
} ue4_pak_t;

// Returns true if 'data' of 'size' bytes has a valid UE4 PAK footer signature.
bool IsUE4Pak (const u8 *data, size_t size);

// Scans and parses the UE4 PAK index table.
enumError ScanUE4Pak (ue4_pak_t *pak, const u8 *data, size_t size);

// Frees memory associated with a scanned pak.
void ResetUE4Pak (ue4_pak_t *pak);

// Extracts a single entry's uncompressed data into 'dest'.
enumError ExtractUE4PakEntry (const ue4_pak_t *pak, uint index, u8 **dest, size_t *dest_size);

// Creates a basic uncompressed UE4 PAK container from a file list.
enumError CreateUE4Pak (u8 **dest, size_t *dest_size, const char *mount_point,
	uint n_files, const char *const *rel_paths, const u8 *const *file_data, const size_t *file_sizes);

#endif // SZS_LIB_UE4PAK_H
