// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_PVOL_H
#define LIB_PVOL_H 1

#include "lib-nintendo.h"

// Pipeworks Volume Archive Container (.vol / .pvol, Godzilla: Unleashed, etc.)
enumError ExtractPVOLArchive (ccp arg, ccp basedir, uint depth);
enumError CreatePVOLArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

enumError create_pvol_dir (ccp source, ccp dest);

#endif // LIB_PVOL_H
