// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// PlatinumGames "DAT" archive (".dat"; MadWorld, Wii). Big-endian, verified
// on all 782 DAT files of the retail disc:
//   0x00 "DAT\0", u32 count, u32 offset_table, u32 ext_table, u32 name_table,
//        u32 size_table, u32 hash_table
//   offset_table: u32 offset[count]; size_table: u32 size[count]
//   name_table:   u32 name_len, then count fixed-width NUL-padded names
//        (already carrying their extension)
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_PLATDAT_H
#define SZS_LIB_PLATDAT_H 1

#include "lib-streamentry.h"
#include <stdio.h>

enumError ScanPlatDat (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries);

#endif
