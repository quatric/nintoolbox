// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_PKZ_H
#define LIB_PKZ_H 1

#include "lib-nintendo.h"

// PlatinumGames Archive (.pkz / pkz)
enumError ExtractPKZArchive (ccp arg, ccp basedir, uint depth);
enumError CreatePKZArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif // LIB_PKZ_H
