// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_PVOL_H
#define LIB_PVOL_H 1

#include "lib-nintendo.h"

// Pikmin 1 & 2 Model/Archive Container (.pvol)
enumError ExtractPVOLArchive (ccp arg, ccp basedir, uint depth);
enumError CreatePVOLArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif // LIB_PVOL_H
