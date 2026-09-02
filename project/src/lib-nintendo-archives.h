// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_NINTENDO_ARCHIVES_H
#define LIB_NINTENDO_ARCHIVES_H 1

#include "types.h"

// Extract Level-5 3DS/Switch Container Archive (.xc / .xpck / XPCK / XPC2)
enumError ExtractXPCKArchive (ccp arg, ccp basedir, uint depth);

// Extract Camelot GameCube/Wii Archive Table (.ztab / ZTAB)
enumError ExtractZTABArchive (ccp arg, ccp basedir, uint depth);

// Extract Dance Dance Revolution Mario Mix Chunk Archive (.mdr)
enumError ExtractMDRArchive (ccp arg, ccp basedir, uint depth);

// Extract Pikmin 1 & 2 Model/Archive Container (.pvol)
enumError ExtractPVOLArchive (ccp arg, ccp basedir, uint depth);

// Extract Jump Super Stars / Jump Ultimate Stars DS Archive (.srd / .stpk / STPK)
enumError ExtractSTPKArchive (ccp arg, ccp basedir, uint depth);

#endif // LIB_NINTENDO_ARCHIVES_H
