// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_PTD_H
#define LIB_PTD_H 1

#include "lib-nintendo.h"

bool IsPTD (const u8 *data, uint size);
enumError ScanPTD (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

#endif // LIB_PTD_H
