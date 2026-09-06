#ifndef LIB_GPKG_H
#define LIB_GPKG_H

#include "lib-nintendo.h"

typedef struct gpkg_entry_t
{
	char name[0x21];
	const u8 *data;
	u32 size;
} gpkg_entry_t;

typedef struct gpkg_t
{
	const u8 *data;
	uint size;
	gpkg_entry_t *entries;
	uint n_entries;
} gpkg_t;

void ResetGPKG (gpkg_t *pkg);
enumError ScanGPKG (gpkg_t *pkg, const u8 *data, uint size);
enumError CreateGPKG (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif
