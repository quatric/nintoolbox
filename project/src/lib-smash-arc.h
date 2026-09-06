#ifndef LIB_SMASH_ARC_H
#define LIB_SMASH_ARC_H

#include "lib-nintendo.h"

#define SMASH_ARC_MAGIC 0xABCDEF9876543210ULL

// Smash Ultimate ARC Header (0x00 - 0x38)
typedef struct smash_arc_header_t
{
	u64 magic;
	u64 stream_section_offset;
	u64 file_section_offset;
	u64 shared_section_offset;
	u64 fs_offset;
	u64 search_offset;
	u64 padding;
} __attribute__((packed)) smash_arc_header_t;

// Detection & type checks
bool IsSmashARC (const u8 *data, size_t size);
bool IsSmashARCFile (ccp filename);

// Extraction helper for wszst xx:
// Extracts directory hierarchy and files (uncompressed and Zstd) from data.arc.
enumError ExtractSmashARC (ccp arg, ccp basedir, uint depth);

#endif // LIB_SMASH_ARC_H
