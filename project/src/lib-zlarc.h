// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_ZLARC_H
#define LIB_ZLARC_H 1

#include "lib-nintendo.h"

// NES Remix indieszero Archive (.zlarc)
enumError ExtractZLARCArchive (ccp arg, ccp basedir, uint depth);
enumError CreateZLARCArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif // LIB_ZLARC_H
