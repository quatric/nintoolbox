// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Bully: Scholarship Edition (Wii) collision archive (DATA/files/Coll/
// collision.pak). A flat, big-endian sector-style table of GTA-era "COL"
// collision chunks (COLL/COL2/COL3 -- the well-documented RenderWare-family
// format used by GTA III/Vice City/San Andreas; see gtamods.com/wiki/
// Collision_File), packed with no compression and no per-entry name.
//
// Layout, verified against the sole real sample on disc (3332248 bytes):
//   0x00  u32 total_file_size (byte-exact against the real file size)
//   0x04  u32 entry_count (486 here, counting this header record itself as
//         entry 0 -- see below)
//   0x08  u32 unknown (constant-looking per file, meaning not recovered)
//   0x0c  (entry_count-1) * 12-byte records:
//           +0x00  u32 data_offset (absolute, from the start of the file)
//           +0x04  u32 data_size
//           +0x08  u32 hash -- some kind of checksum or a name hash (most
//                  likely a jenkins/CRC32 of the original model name this
//                  variant no longer stores, since the COL chunk's own
//                  usual name field reads as all zero bytes here); the
//                  algorithm was not recovered, so this decoder just
//                  reports it in each member's filename rather than
//                  guessing which hash function it is.
//   The table (entry_count*12 bytes total, including the header record) is
//   padded up to the next 16-byte boundary, and entry[0]'s data_offset
//   starts exactly there -- verified via the same "next.data_offset ==
//   this.data_offset+this.data_size" chain check the IDE.dir/img decoder
//   uses, holding for every one of the 485 real entries here.
//   The last entry's end sits 144 bytes short of the declared total file
//   size; those trailing bytes are an unrecovered footer, left untouched.
//
// Every one of the 485 real entries on disc starts with a recognized COL
// FourCC (COL3: 398, COL2: 69, COLL: 18) -- this decoder does not parse
// the COL format itself (it is already documented elsewhere), it only
// slices the container into individual ".col" files for an external COL
// tool/viewer to open.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_COLLPAK_H
#define SZS_LIB_COLLPAK_H 1

#include "lib-std.h"

enumError ExtractCollPakArchive (ccp arg, ccp basedir, uint depth);

#endif
