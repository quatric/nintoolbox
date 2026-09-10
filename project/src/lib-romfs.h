// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_ROMFS_H
#define LIB_ROMFS_H 1

#include "lib-nintendo.h"

// Nintendo 3DS RomFS Archive (.romfs / IVFC)
enumError ExtractROMFSArchive (ccp arg, ccp basedir, uint depth);

#endif // LIB_ROMFS_H
