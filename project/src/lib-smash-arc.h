#ifndef SZS_LIB_SMASH_ARC_H
#define SZS_LIB_SMASH_ARC_H 1

#include "types.h"
#include "file-type.h"

// Super Smash Bros. Ultimate data.arc container format.

typedef struct smash_arc_file_t
{
	char filename[PATH_MAX];
	u64 offset;
	u64 comp_size;
	u64 decomp_size;
	bool is_zstd;
} smash_arc_file_t;

typedef struct smash_arc_t
{
	const u8 *data;
	size_t size;
	u64 music_stream_size;
	u64 table_offset;
	u64 table_size;
	uint n_files;
	smash_arc_file_t *files;
} smash_arc_t;

// Returns true if 'data' is a Smash Ultimate data.arc container.
bool IsSmashArc (const u8 *data, size_t size);

// True for a retail Super Smash Bros. Ultimate data.arc, whose filesystem
// this reader does not implement.
bool IsRetailSmashArc (const u8 *data, size_t size);

// Scans and parses the data.arc structure.
enumError ScanSmashArc (smash_arc_t *arc, const u8 *data, size_t size);

// Frees memory associated with smash_arc_t.
void ResetSmashArc (smash_arc_t *arc);

// Extracts a single file entry from data.arc.
enumError ExtractSmashArcEntry (const smash_arc_t *arc, uint index, u8 **dest, size_t *dest_size);

// Creates a basic Smash ARC container from file data.
enumError CreateSmashArc (u8 **dest, size_t *dest_size, uint n_files,
	const char *const *paths, const u8 *const *file_data, const size_t *file_sizes);

#endif // SZS_LIB_SMASH_ARC_H
