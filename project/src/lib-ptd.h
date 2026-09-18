// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_PTD_H
#define LIB_PTD_H 1

#include "lib-nintendo.h"

bool IsPTD (const u8 *data, uint size);
enumError ScanPTD (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

// Full PTD container model preserving every field.
// Reference: MPLibrary/GCWii/Audio/PTD.cs PtdFile::Read/Write. Mono files
// carry one channel, stereo files (flags & 0x01000000) two; a file whose
// flags == 23871488 carries an extra 144-byte UnknownData blob after its
// channel headers. nibble_count is the ADPCM nibble length shared by both
// channels; each channel's data is nibble_count/2 bytes.
typedef struct ptd_channel_t
{
	u16 coef[16];
	u16 unknown;
	const u8 *data;
	uint data_size;
} ptd_channel_t;

typedef struct ptd_stream_t
{
	u32 flags;
	u32 sample_rate;
	u32 nibble_count;
	u32 loop_start;
	ptd_channel_t channels[2];
	uint num_channels;
	const u8 *unknown_data;
	uint unknown_size;
} ptd_stream_t;

typedef struct ptd_file_t
{
	u16 version;
	u32 unknown2;
	u32 sample_rate;
	u32 channel_count;
	ptd_stream_t *streams;
	uint num_streams;
} ptd_file_t;

enumError ScanPTDFile (ptd_file_t *file, const u8 *data, uint size);
void ResetPTDFile (ptd_file_t *file);
// Inverse of ScanPTDFile: rebuilds the container byte-for-byte from the
// parsed model (coefficient indices are reassigned in channel order).
enumError CreatePTD (u8 **dest, uint *dest_size, const ptd_file_t *file);

#endif // LIB_PTD_H
