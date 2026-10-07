// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Alchemy/Vicarious Visions "IGA" archive (".arc"; Skylanders on Wii).
// Little-endian, verified on retail Spyro's Adventure archives:
//   0x00 "IGA\x1a", u32 version (4), u32 toc_size, u32 count, u32 ?,
//        u32 ?, u32 names_offset, u32 names_size, 0x10 reserved
//   0x30 u32 name_hash[count] (sorted)
//        {u32 offset, u32 size, u32 flags(-1 = stored)}[count]
//   names_offset: u32 name_offset[count] (relative to names_offset, pointing
//        into the string pool that follows the table); entry i uses name i.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_IGA_H
#define SZS_LIB_IGA_H 1

#include "lib-streamentry.h"
#include <stdio.h>

enumError ScanIga (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries);

#endif
