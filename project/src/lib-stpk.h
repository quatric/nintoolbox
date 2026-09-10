// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_STPK_H
#define LIB_STPK_H 1

#include "lib-nintendo.h"

// Jump Super Stars / Jump Ultimate Stars DS Archive (.srd / .stpk / STPK)
enumError ExtractSTPKArchive (ccp arg, ccp basedir, uint depth);
enumError CreateSTPKArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif // LIB_STPK_H
