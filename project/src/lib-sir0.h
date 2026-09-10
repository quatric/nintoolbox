// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_SIR0_H
#define LIB_SIR0_H 1

#include "lib-nintendo.h"

// Pokemon Mystery Dungeon Resource Container (.sir0 / SIR0)
enumError ExtractSIR0Archive (ccp arg, ccp basedir, uint depth);
enumError CreateSIR0Archive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif // LIB_SIR0_H
