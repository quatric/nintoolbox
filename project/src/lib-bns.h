#ifndef LIB_BNS_H
#define LIB_BNS_H

#include "lib-nintendo.h"

typedef struct bns_entry_t
{
	const u8 *data;
	u32 size;
} bns_entry_t;

typedef struct bns_t
{
	const u8 *data;
	uint size;
	bns_entry_t *entries;
	uint n_entries;
} bns_t;

void ResetBNS (bns_t *bns);
enumError ScanBNS (bns_t *bns, const u8 *data, uint size);

#endif
