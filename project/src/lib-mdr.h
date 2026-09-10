// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_MDR_H
#define LIB_MDR_H 1

#include "lib-nintendo.h"

// Dance Dance Revolution Mario Mix Chunk Archive (.mdr)
enumError ExtractMDRArchive (ccp arg, ccp basedir, uint depth);
enumError CreateMDRArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif // LIB_MDR_H
