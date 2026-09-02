#ifndef LIB_HWL_H
#define LIB_HWL_H

#include "lib-nintendo.h"

enumError ScanHWLegends (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *idx,
	uint idx_size, const u8 *bin, uint bin_size);
enumError CreateHWLegends (u8 **dest_idx, uint *dest_idx_size, u8 **dest_bin, uint *dest_bin_size,
	const nintendo_sarc_entry_t *entries, uint n_entries);

#endif
