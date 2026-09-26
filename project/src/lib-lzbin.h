// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_LZBIN_H
#define LIB_LZBIN_H 1

#include "lib-nintendo.h"

bool IsLZBIN (const u8 *data, uint size);
enumError ScanLZBIN (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);
enumError CreateLZBIN (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

bool looks_like_lzbin_dir (ccp dir);
enumError create_lzbin_dir (ccp source, ccp dest);

#endif // LIB_LZBIN_H
