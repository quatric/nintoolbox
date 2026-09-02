#ifndef LIB_DARC_H
#define LIB_DARC_H

#include "lib-nintendo.h"

typedef struct darc_entry_t
{
	char *name;
	bool is_dir;
	u32 parent_or_offset;
	u32 end_or_size;
} darc_entry_t;

typedef struct darc_t
{
	const u8 *data;
	uint size;
	darc_entry_t *entries;
	uint n_entries;
} darc_t;

void ResetDARC (darc_t *darc);
enumError ScanDARC (darc_t *darc, const u8 *data, uint size);
enumError CreateDARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif
