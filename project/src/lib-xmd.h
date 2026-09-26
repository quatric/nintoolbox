// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_XMD_H
#define LIB_XMD_H 1

#include "lib-nintendo.h"

// Genki "GTI Club: Supermini Festa!" (Wii) car model container (.mdl).
// A thin "XMD" wrapper that concatenates two otherwise independently
// distributed files: a "RESOURCE:GX" geometry container (byte-identical
// to the sibling ".r3d" file) followed by an "XTD\0" texture table
// (byte-identical to the sibling ".unq.xtd" file). See lib-xtd.h for the
// texture table layout; the geometry container's own display-list/vertex
// format is not yet decoded.
bool IsXMD (const u8 *data, uint size);
enumError ExtractXMDArchive (ccp arg, ccp basedir, uint depth);

#endif // LIB_XMD_H
