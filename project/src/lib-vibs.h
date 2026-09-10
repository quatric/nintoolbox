// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_VIBS_H
#define LIB_VIBS_H 1

#include "lib-nintendo.h"

// Nintendo Switch Joy-Con Vibration Archive (.vibs)
enumError ExtractVIBSArchive (ccp arg, ccp basedir, uint depth);
enumError CreateVIBSArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif // LIB_VIBS_H
