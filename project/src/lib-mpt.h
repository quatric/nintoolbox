// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Nordcurrent MPT Texture (.mpt)
// Used in Wii titles developed by Nordcurrent (101-in-1 Party Megamix, etc.)
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_MPT_H
#define SZS_LIB_MPT_H 1

#include "lib-nintendo.h"
#include <stdio.h>

struct Image_t;

bool IsMPT (const u8 *data, size_t size);
enumError SaveMPT (struct Image_t *img, FILE *f, ccp fname, bool overwrite);

#endif // SZS_LIB_MPT_H
