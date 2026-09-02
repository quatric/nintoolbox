#ifndef LIB_SOUND_ARCHIVE_H
#define LIB_SOUND_ARCHIVE_H

#include "lib-nintendo.h"

typedef struct sar_file_entry_t
{
	u32 file_id;
	char *name;
	const u8 *data;
	u32 size;
	u32 offset;
	u16 type_id;
	char ext[8];
} sar_file_entry_t;

typedef struct sound_archive_t
{
	const u8 *raw;
	size_t raw_size;
	bool is_big_endian;
	bool is_cafe_or_switch;
	char magic[5];
	u32 version;
	sar_file_entry_t *entries;
	uint n_entries;
} sound_archive_t;

void ResetSoundArchive (sound_archive_t *sar);
enumError ScanSoundArchive (sound_archive_t *sar, const u8 *data, size_t size);
enumError CreateSoundArchive (u8 **dest, uint *dest_size, const sound_archive_t *sar);

#endif
