#ifndef LIB_RPAK_H
#define LIB_RPAK_H

#include "lib-nintendo.h"

typedef struct rpak_entry_t
{
	const u8 *data;
	u32 size;
	u32 magic;
	u32 id_hi;
	u32 id_lo;
	bool compressed;
} rpak_entry_t;

typedef struct rpak_t
{
	const u8 *data;
	uint size;
	rpak_entry_t *entries;
	uint n_entries;
} rpak_t;

void ResetRPAK (rpak_t *pak);
enumError ScanRPAK (rpak_t *pak, const u8 *data, uint size);
u8 *DecompressRPAKEntry (const u8 *data, uint size, uint *res_size);

#endif
