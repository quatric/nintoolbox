// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "FCAT" container (".dat"; Winning Post 7/World, Wii). Big-endian:
//   0x00 "FCAT", u32 count, {u32 offset, u32 size}[count]
// Members are stored raw (mostly "bres" BRRES files) and are named by index
// plus an extension derived from their magic.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_FCAT_H
#define SZS_LIB_FCAT_H 1

#include "lib-streamentry.h"
#include <stdio.h>

enumError ScanFcat (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries);

#endif
