#ifndef LIB_FSYS_H
#define LIB_FSYS_H

#include "lib-nintendo.h"

enumError ScanFSYS (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

enumError CreateFSYS (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, bool compress);

#endif
