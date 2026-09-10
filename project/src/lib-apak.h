// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_APAK_H
#define LIB_APAK_H 1

#include "lib-nintendo.h"

// Nintendo APAK Archive (.apak / APAK)
enumError ExtractAPAKArchive (ccp arg, ccp basedir, uint depth);
enumError CreateAPAKArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif // LIB_APAK_H
