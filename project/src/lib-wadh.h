// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Data Design Interactive "WADH" game archives (DataWII.wad in Ninjabread
// Man, Wii). Not related to Wii channel WADs.
//
// Layout, all fields little-endian:
//   Header (0x10 bytes)
//     0x00 char[4] "WADH"
//     0x04 u32 data_base   (== size of header + directory + name table;
//                           member offsets are relative to it)
//     0x08 u32 n_entries
//     0x0c u32 names_size  (size of the NUL-separated name table)
//   Directory, n_entries * 32 bytes, entry 0 is the anonymous root:
//     u32 name_offset (into the name table, 0xffffffff for the root)
//     u32 name_hash
//     u32 offset      (files only, relative to data_base)
//     u32 size
//     u32 size2       (== size; members are stored uncompressed)
//     u32 flags
//     u32 last_child  (directories: index of the LAST child;
//                      files: 0xffffffff)
//     u32 prev_sibling (index of the previous entry in the same directory,
//                      0xffffffff for the first)
//   Name table directly follows the directory.
// Directories are entries with size == 0 and last_child != 0xffffffff.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_WADH_H
#define SZS_LIB_WADH_H 1

#include "lib-nintendo.h"

enumError ScanWADH (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

#endif
