// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Nordcurrent BigFile PK2 Multi-Part Archive (.pk2)
// Used in Wii titles developed by Nordcurrent (101-in-1 Party Megamix, etc.)
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_PK2_H
#define SZS_LIB_PK2_H 1

#include "lib-nintendo.h"
#include <stdio.h>

bool IsPK2 (const u8 *data, size_t size);
enumError ExtractPK2Archive (ccp arg, ccp basedir, uint depth);
enumError create_pk2_dir (ccp source_dir, ccp dest_file);

#endif // SZS_LIB_PK2_H
