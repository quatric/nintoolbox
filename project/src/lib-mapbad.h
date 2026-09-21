// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Collision Studios level "map.bad" navigation/area data (.bad, "Brave: A
// Warrior's Tale"). Magic "BAD" + u8 version (4 in every sample).
//
// Layout, all fields big-endian:
//   0x00  "BAD" + u8 version
//   0x04  u32 area_count
//   0x08  u32 link_count
//   0x0c  area_count * 32-byte records:
//           +0x00 u32 a, +0x04 u32 b (meaning not recovered -- see below)
//           +0x08 u32 0xffffffff (a fixed sentinel; verified on every one
//                 of area_count records across 7 samples from 240 bytes to
//                 342572 bytes, including counts from 1 to 274 -- this is
//                 the gate this decoder uses to trust the record layout)
//           +0x0c 8 zero bytes
//           +0x14 3 floats, (1.0, 1.0, 1.0) in every sample (a scale?)
//   (0x0c + area_count*32)  link_count * 32-byte records, same stride,
//           fields not recovered (no invariant found to verify them by)
//   remainder: everything after the link table. `a` in the area records
//           climbs roughly monotonically in step with the record index
//           (bar record 0, which looks like a summary/total) in a way
//           that suggests it indexes into this remainder, but that could
//           not be confirmed against real geometry, so the remainder is
//           carved out untouched as geometry.bin rather than sliced by
//           guesswork.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_MAPBAD_H
#define SZS_LIB_MAPBAD_H 1

#include "lib-std.h"

enumError ExtractMapBadArchive (ccp arg, ccp basedir, uint depth);

#endif
