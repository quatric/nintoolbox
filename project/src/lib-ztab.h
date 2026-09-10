// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_ZTAB_H
#define LIB_ZTAB_H 1

#include "lib-nintendo.h"

// Camelot GameCube/Wii Archive Table (.ztab / ZTAB)
enumError ExtractZTABArchive (ccp arg, ccp basedir, uint depth);
enumError CreateZTABArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif // LIB_ZTAB_H
