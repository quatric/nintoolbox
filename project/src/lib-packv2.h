// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "PACK" version 2 data archive (".pak"; Ski and Shoot / RTL Biathlon 2009 and
// six more Wii titles: packfile.pak). Little-endian, verified on the
// 2,848-entry retail packfile.pak of Ski and Shoot:
//   0x00 "PACK", u32 version (2), u32 n_entries, u32 table_end
//   0x10 n_entries records of 0x18 bytes:
//        u32 size, u32 size2, u32 offset, u32 name_offset (absolute, NUL
//        terminated "dir/file.ext" path), u32 crc, u32 flags
// Flags 0x30: stored (size == size2). Flags 0x77: packed (size = packed bytes,
// size2 = unpacked bytes) with a bit-oriented LZ coder that is not decoded
// yet; those members are written unchanged as "<name>.packed".
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_PACKV2_H
#define SZS_LIB_PACKV2_H 1

#include "lib-streamentry.h"
#include <stdio.h>

enumError ScanPackV2 (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries);

#endif
