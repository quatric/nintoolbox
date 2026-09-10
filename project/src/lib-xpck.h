// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_XPCK_H
#define LIB_XPCK_H 1

#include "lib-nintendo.h"

// Level-5 3DS/Switch Container Archive (.xc / .xpck / XPCK / XPC2)
enumError ExtractXPCKArchive (ccp arg, ccp basedir, uint depth);
enumError CreateXPCKArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif // LIB_XPCK_H
