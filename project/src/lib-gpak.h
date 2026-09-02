#ifndef LIB_GPAK_H
#define LIB_GPAK_H

#include "lib-nintendo.h"

typedef struct gpak_entry_t
{
	const u8 *data;
	u32 size;
} gpak_entry_t;

typedef struct gpak_t
{
	const u8 *data;
	uint size;
	gpak_entry_t *entries;
	uint n_entries;
} gpak_t;

void ResetGPAK (gpak_t *pak);
enumError ScanGPAK (gpak_t *pak, const u8 *data, uint size);

#endif
