// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Grip Entertainment ".res" packages (Sesame Street: Elmo's Musical
// Monsterpiece, Wii). Big-endian serialised resource images.
//
//   0x00 "res\n", 0x04 u32 version (0x0c010500)
//   0x0c u32 data_base   (first section, everything below is relative to it)
//   0x2c u32 table_off   (section table, at the end of the file)
//   0x30 u32 table_size  (table_off + table_size == file size)
//   0x3c u32 n_types, 0x40: n_types * { char fourcc[4], u16 id, u16 0 }
//   section table: u32 n_sections, u32 4, then per section (24 bytes)
//     { char fourcc[4], u32 offset (from data_base), u32 size, u32 align,
//       u32 count, u32 extra }
//   Section 0 of type "strg" holds the resource names (NUL separated), the
//   "indx" section the public resources: u32 count, u32 4, then per entry
//   { i32 name (relative to the field's own address), char fourcc[4],
//     u32 offset into the type's address space }. Objects reference their
//   unnamed sub-resources (surf, gshd, bmsh, ...) by pointer, so those are
//   exported as plain per-section files.
//
// "surf" sections: 0x40-byte header (u16 width at 0x10, u16 height at 0x12,
// u8 format at 0x0f: 2 = GX CMPR, 6 = GX RGB5A3, 3 = GX I8 whose data offset
// is the u32 at 0x20), then the top mip at 0x40, rows stored bottom-up.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_GRIPRES_H
#define SZS_LIB_GRIPRES_H 1

#include "lib-nintendo.h"

enumError ScanGripRES (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

bool IsGripSurf (const u8 *data, uint size);
enumError DecodeGripSurf (u8 **rgba, uint *width, uint *height, const u8 *data, uint size);

#endif
