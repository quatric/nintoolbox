// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_NXARC_H
#define LIB_NXARC_H 1

#include "lib-nintendo.h"

// Nintendo Switch NX Archive (.nxarc / RAXN)
enumError ExtractNXARCArchive (ccp arg, ccp basedir, uint depth);
enumError CreateNXARCArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

enumError create_nxarc_dir (ccp source, ccp dest);

#endif // LIB_NXARC_H
