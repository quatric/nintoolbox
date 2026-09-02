#ifndef LIB_NCCARC_H
#define LIB_NCCARC_H

#include "lib-nintendo.h"

typedef struct nccarc_entry_t
{
	const u8 *data;
	u32 size;
	bool flag;
} nccarc_entry_t;

typedef struct nccarc_t
{
	const u8 *data;
	uint size;
	nccarc_entry_t *entries;
	uint n_entries;
} nccarc_t;

void ResetNCCARC (nccarc_t *nc);
enumError ScanNCCARC (nccarc_t *nc, const u8 *data, uint size);
enumError CreateNCCARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif
