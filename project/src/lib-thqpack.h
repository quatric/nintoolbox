// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "pack" data archive of the THQ Studio Australia Nickelodeon Avatar games
// (Nickelodeon Avatar: The Last Airbender and its sequels, Wii; files like
// c2_DATA.PAK, mn_DATA.PAK). All fields big-endian, verified on nine retail
// files:
//   0x00 char[4] "pack", u32 version (1), u32 names_size
//   0x0c u32 total_size (the file size rounded up to 0x800)
//   0x10 u32 names_offset, u32 n_entries
//   0x18 entries of 16 bytes: u32 name_offset (into the name table),
//        u32 offset, u32 size, u32 zero
// Names are NUL-terminated "data/..." paths; payloads sit on 0x800 boundaries
// and are stored as is (they are usually inner .pak/.rad files).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_THQPACK_H
#define SZS_LIB_THQPACK_H 1

#include "lib-streamentry.h"
#include <stdio.h>

enumError ScanThqPack (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries);

#endif
