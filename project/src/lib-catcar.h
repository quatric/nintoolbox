// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Cat Daddy Games "CDGaCube" archives (birthday.CAR in Birthday Party Bash,
// Wii). All fields little-endian.
//
//   0x00 char[8] "CDGaCube"
//   0x08 u32 data_sector   (first member's sector; the name table ends before it)
//   0x0c u32 n_entries
//   0x10 u32 entry_size    (24)
//   0x14 entries, n_entries * 24 bytes:
//          u32 flags       (0x10 "."/".." of the root, 0x30 directory,
//                           0x20 / 0x21 file)
//          u64 filetime    (ignored)
//          u32 size        (bytes stored in the archive)
//          u32 name_index  (== entry index)
//          u32 sector      (member offset / 2048)
//   after the entries: n_entries NUL-terminated full paths, in entry order.
// Members are stored raw or as a bare zlib stream; a member is treated as
// zlib when a stream starting at its offset ends exactly after `size` bytes.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_CATCAR_H
#define SZS_LIB_CATCAR_H 1

#include "lib-nintendo.h"

enumError ScanCatCar (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
