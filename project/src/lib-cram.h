#ifndef LIB_CRAM_H
#define LIB_CRAM_H

#include "lib-nintendo.h"

enumError ScanCramARC (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);
enumError CreateCramARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif
