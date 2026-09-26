// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_XTD_H
#define LIB_XTD_H 1

#include "lib-nintendo.h"

// Genki "GTI Club: Supermini Festa!" (Wii) car asset resource table
// (.unq.xtd). A little-endian directory of tagged sub-resources (seen so
// far: raw Nintendo TPL textures, each preceded by a fixed 0x80-byte
// sub-header) -- distinct from the game's own big-endian ".r3d" GX
// resource containers.
bool IsXTD (const u8 *data, uint size);
enumError ExtractXTDArchive (ccp arg, ccp basedir, uint depth);

// Shared with lib-xmd.c, whose ".mdl" files embed a byte-identical XTD
// block alongside their geometry. 'dest' must already exist.
enumError ExtractXTDBuffer (const u8 *data, uint size, ccp dest, bool print_msg, ccp src_name);

#endif // LIB_XTD_H
