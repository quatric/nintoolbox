// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "MRQZ" archive (".pak"; My Fitness Coach). Little-endian, verified on all
// 565 retail paks:
//   0x00 "MRQZ", u32 end (= file_size - 8), u16 version (2), u16 hdr_size
//        (0x80 or 0x1000), u32 count, ...
//   hdr_size+8: {char name[64], u32 offset, u32 size, u32 0}[count]
//   data block at align(hdr_size + 8 + 0x4c*count, hdr_size); offsets are
//        relative to it and members are stored raw.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_MRQZ_H
#define SZS_LIB_MRQZ_H 1

#include "lib-streamentry.h"
#include <stdio.h>

enumError ScanMrqz (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries);

#endif
