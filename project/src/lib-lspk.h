#ifndef LIB_LSPK_H
#define LIB_LSPK_H

#include "lib-nintendo.h"

typedef struct lspk_entry_t
{
	const u8 *data;
	u32 size;
	u32 dec_size;
	u32 hash;
} lspk_entry_t;

typedef struct lspk_t
{
	const u8 *pk_data;
	uint pk_size;
	lspk_entry_t *entries;
	uint n_entries;
} lspk_t;

void ResetLSPK (lspk_t *pak);
enumError ScanLSPK (
	lspk_t *pak, const u8 *pkh_data, uint pkh_size, const u8 *pk_data, uint pk_size);

#endif
