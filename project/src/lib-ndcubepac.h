// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_NDCUBEPAC_H
#define LIB_NDCUBEPAC_H 1

#include "lib-nintendo.h"

// Nd Cube Wii U flat container (.bin / "PAC\0", Mario Party 10 / Amiibo Festival)
enumError ExtractPACArchive (ccp arg, ccp basedir, uint depth);

#endif // LIB_NDCUBEPAC_H
