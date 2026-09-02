#ifndef LIB_SFZDAT_H
#define LIB_SFZDAT_H

#include "lib-nintendo.h"

enumError ScanSFZDAT (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);
enumError CreateSFZDAT (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif
