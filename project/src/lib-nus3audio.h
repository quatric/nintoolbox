// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_NUS3AUDIO_H
#define LIB_NUS3AUDIO_H 1

#include "lib-nintendo.h"

// Bandai Namco NUS3AUDIO audio archive (.nus3audio / NUS3)
enumError ExtractNUS3AudioArchive (ccp arg, ccp basedir, uint depth);
enumError CreateNUS3AudioArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif // LIB_NUS3AUDIO_H
