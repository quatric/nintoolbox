// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_MTXT_H
#define LIB_MTXT_H 1

#include "lib-nintendo.h"

// Nintendo Switch MTXT Texture Archive (.mtxt / MTXT)
enumError ExtractMTXTArchive (ccp arg, ccp basedir, uint depth);
enumError CreateMTXTArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

enumError create_mtxt_dir (ccp source, ccp dest);

#endif // LIB_MTXT_H
