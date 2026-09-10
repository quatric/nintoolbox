// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_F9RES_H
#define LIB_F9RES_H 1

#include "lib-nintendo.h"

// GameCube Resource Archive (.res / res\n)
enumError ExtractF9ResArchive (ccp arg, ccp basedir, uint depth);
enumError CreateF9ResArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif // LIB_F9RES_H
