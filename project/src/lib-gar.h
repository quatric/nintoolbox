// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_GAR_H
#define LIB_GAR_H 1

#include "lib-nintendo.h"

// Grezzo Zelda / Luigi's Mansion 3DS Archive (.zar / .gar / ZAR / GAR)
enumError ExtractGARArchive (ccp arg, ccp basedir, uint depth);
enumError CreateGARArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

enumError create_gar_dir (ccp source, ccp dest);

#endif // LIB_GAR_H
