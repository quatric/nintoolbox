// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_TMPK_H
#define LIB_TMPK_H 1

#include "lib-nintendo.h"

// Twilight Princess HD / Zelda TMPK Archive (.pack / TMPK)
enumError ExtractTMPKArchive (ccp arg, ccp basedir, uint depth);
enumError CreateTMPKArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif // LIB_TMPK_H
