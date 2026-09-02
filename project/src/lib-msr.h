#ifndef LIB_MSR_H
#define LIB_MSR_H

#include "lib-nintendo.h"

enumError ScanMetroidSR (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

#endif
