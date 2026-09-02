#ifndef LIB_WARC_H
#define LIB_WARC_H

#include "lib-nintendo.h"

typedef struct warc_entry_t
{
	char *name;
	const u8 *data;
	u32 size;
} warc_entry_t;

typedef struct warc_t
{
	const u8 *data;
	uint size;
	warc_entry_t *entries;
	uint n_entries;
	char *names;
} warc_t;

void ResetWARC (warc_t *warc);
enumError ScanWARC (warc_t *warc, const u8 *data, uint size);
enumError CreateWARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif
