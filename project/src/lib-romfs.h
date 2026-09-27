// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_ROMFS_H
#define LIB_ROMFS_H 1

#include "lib-nintendo.h"

// Nintendo 3DS / Switch RomFS Archive (.romfs / IVFC)
bool IsROMFS (const u8 *data, uint size);
enumError ExtractROMFSArchive (ccp arg, ccp basedir, uint depth);

#endif // LIB_ROMFS_H
