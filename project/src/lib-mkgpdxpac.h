// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_MKGPDXPAC_H
#define LIB_MKGPDXPAC_H 1

#include "lib-nintendo.h"

// Mario Kart Arcade GP DX Layout Archive (.pac / pack)
enumError ExtractMKGPDXPacArchive (ccp arg, ccp basedir, uint depth);
enumError CreateMKGPDXPacArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif // LIB_MKGPDXPAC_H
