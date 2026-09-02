#ifndef SZS_LIB_NUS3_H
#define SZS_LIB_NUS3_H 1

#include "types.h"
#include "file-type.h"

// Namco Universal Sound 3 (NUS3BANK / NUS3AUDIO) container format used in
// Super Smash Bros. 3DS, Super Smash Bros. for Wii U, and Super Smash Bros. Ultimate.

typedef struct nus3_track_t
{
	uint track_id;
	char name[256];
	u32 offset; // offset within PACK section or file
	u32 size;   // size of audio payload
	const u8 *data;
	char ext[16]; // inferred extension: .idsp, .lopus, .dsp, .bns, .wav, .bin
} nus3_track_t;

typedef struct nus3_t
{
	const u8 *data;
	size_t size;
	bool is_big_endian;
	uint total_size;
	uint n_tracks;
	nus3_track_t *tracks;
	const u8 *pack_data;
	size_t pack_size;
} nus3_t;

// Returns true if 'data' starts with NUS3 magic ("NUS3" or "nus3" in LE/BE).
bool IsNUS3 (const u8 *data, size_t size);

// Scans and parses the NUS3 container sections.
enumError ScanNUS3 (nus3_t *nus, const u8 *data, size_t size);

// Frees allocated tracks in nus.
void ResetNUS3 (nus3_t *nus);

// Creates a basic NUS3AUDIO container wrapping one or more audio streams.
enumError CreateNUS3Audio (u8 **dest, size_t *dest_size, uint n_tracks,
	const char *const *names, const u32 *track_ids, const u8 *const *audio_data, const size_t *audio_sizes);

#endif // SZS_LIB_NUS3_H
