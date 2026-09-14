// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_F9RES_H
#define LIB_F9RES_H 1

#include "lib-nintendo.h"

// GameCube Resource Archive (.res / res\n)
enumError ExtractF9ResArchive (ccp arg, ccp basedir, uint depth);
enumError CreateF9ResArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

enumError create_f9res_dir (ccp source, ccp dest);

#endif // LIB_F9RES_H
