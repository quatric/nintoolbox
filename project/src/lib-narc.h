#ifndef LIB_NARC_H
#define LIB_NARC_H

#include "lib-nintendo.h"

typedef struct narc_entry_t
{
	char *name;
	u32 offset;
	u32 size;
} narc_entry_t;

typedef struct narc_t
{
	const u8 *raw;
	size_t raw_size;
	bool is_le;
	narc_entry_t *entries;
	uint n_entries;
	const u8 *fimg_data;
	uint fimg_size;
} narc_t;

void ResetNARC (narc_t *narc);
enumError ScanNARC (narc_t *narc, const u8 *data, size_t size);
enumError CreateNARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, bool is_le);

#endif
