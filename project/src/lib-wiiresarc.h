// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "wii\0" resource archive (".ARC"; *Petanque Master* and *Petanque Pro*
// on Wii: 0_MENUBASE.ARC ... PARK.ARC, MUSIC.ARC, *SOUNDS.ARC; seven more
// titles share the magic). Big-endian, verified on 105 retail files:
//   0x00 "wii\0", u32 0, u32 w2, u32 n_entries, u32 flag
//   0x14 u32 names_offset, 0x18 u32 data_base, ...
//   records at names_offset - 0x20 * n_entries, 0x20 bytes each:
//     u32 runtime, u32 type, u32 size, u32 offset, u32 id, u32 0, u32 0x20,
//     u32 runtime
//   names_offset: n_entries u32 absolute offsets of NUL-terminated names
// Record offsets are relative to data_base when the first record's offset is
// 0 (the level and menu archives) and absolute otherwise (the *SOUNDS.ARC
// banks). Members come out as "<name>.t<type>"; the i-th name belongs to the
// i-th record. Payloads are stored as is.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_WIIRESARC_H
#define SZS_LIB_WIIRESARC_H 1

#include "lib-streamentry.h"
#include <stdio.h>

enumError ScanWiiResArc (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries);

#endif
