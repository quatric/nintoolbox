#ifndef LIB_BG4_H
#define LIB_BG4_H

#include "lib-nintendo.h"

enumError ScanBG4 (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);
enumError CreateBG4 (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif
