// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_HBDF_H
#define LIB_HBDF_H 1

#include "lib-nintendo.h"

bool IsHBDF (const u8 *data, uint size);
enumError ScanHBDF (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

#endif // LIB_HBDF_H
