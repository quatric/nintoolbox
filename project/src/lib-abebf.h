// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Ubisoft Magma "ABE" BigFile (".BF", Rabbids Go Home: RGH.BF,
// RGH.wii.sns.BF, RGH.$hd$.bik.BF, ...). Little-endian, verified on the three
// retail files of the USA disc (15,598 records):
//   0x00 "ABE\0", u32 version (4), u32 total_files, ...
//   0x18 u32 table_offset (0x14d8, the area before is zero padding)
// The table is a chain of chunks:
//   u32 n_slots, u32 prev_or_flag, u32 next_chunk_offset, then n_slots
//   records of 200 bytes. The next chunk is only valid when the second word is
//   0 and the third points inside the file; 0xaaaaaaaa / -1 end the chain.
// A record (empty slots start with a NUL name):
//   +0x00 char name[0x50]  "xxxxxxxx.ext", or "xxxxxxxx\0ext" (key-named)
//   +0x58 u32 size + 0x20, +0x64 u32 key, +0x6c u32 data offset
// The data offset holds a 0x20-byte header: u32 stored_size, u32 unpacked_size,
// u32 0, u32 type, then the payload at +0x20. Types: 2 stored (sizes equal);
// 4 LZO1X blocks (u32 n_blocks, n * u32 packed size, then the 256 KiB blocks back to back); 3 a
// "<$shadow$>" reference to a file living in a sibling BF (skipped); 0 other
// records (skipped).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_ABEBF_H
#define SZS_LIB_ABEBF_H 1

#include "lib-streamentry.h"
#include <stdio.h>

enumError ScanAbeBf (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries);

#endif
