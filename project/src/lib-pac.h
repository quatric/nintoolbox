#ifndef LIB_PAC_H
#define LIB_PAC_H

#include "lib-nintendo.h"

typedef struct pac_entry_t
{
	u16 type;
	u16 index;
	u8 group_index;
	s16 redirect_index;
	char name[16];
	const u8 *data;
	u32 size;
} pac_entry_t;

typedef struct pac_t
{
	const u8 *data;
	uint size;
	char name[48];
	pac_entry_t *entries;
	uint n_entries;
} pac_t;

void ResetPAC (pac_t *pac);
enumError ScanPAC (pac_t *pac, const u8 *data, uint size);
enumError CreatePAC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif
