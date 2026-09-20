// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// h.a.n.d. "FBC" file bundles (.fbc; Oyako de Asobo: Miffy no Omochabako, Wii).
// Big-endian:
//   0x00 u16 0x0014, u16 6, u32 n files, 6 u32 (table offsets, unused here)
//   0x40 n * u32 member offsets, relative to 0x40 (member 0 follows the three
//        pointer tables, so nested bundles start right after the header)
// The last 2 * n 32-byte slots of the file are the size table (u32 + '0'
// padding) followed by the name table (NUL terminated, '0' padded). Members
// are back to back; nested bundles carry the same layout.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_FBC_H
#define SZS_LIB_FBC_H 1

#include "lib-nintendo.h"

bool IsFBC (const u8 *data, size_t size);
enumError ScanFBC (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
