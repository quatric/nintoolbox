#ifndef LIB_RFLRES_H
#define LIB_RFLRES_H

#include "lib-nintendo.h"

enumError ScanRFLRes (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);
enumError CreateRFLRes (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif
