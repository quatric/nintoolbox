// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Neversoft Guitar Hero (Wii) ".pak.ngc" packages and ".img.ngc" textures.
// Layout worked out from the 2654 .pak.ngc and 3465 .img.ngc files on the
// retail disc of Guitar Hero: Smash Hits (USA); all .img.ngc and all but two
// .pak.ngc satisfy it. Big-endian.
//
// .pak.ngc: a list of entry headers, then payloads addressed from the file
// start. Header, 0x20 bytes:
//   +0x00 u32 type    CRC of the file extension (.qb, .tex, ...)
//   +0x04 u32 offset  payload offset from the file start
//   +0x08 u32 size
//   +0x0c u32 crc     CRC of the full name
//   +0x10 u32 0
//   +0x14 u32 crc     CRC of the name without path/extension
//   +0x18 u32 0
//   +0x1c u32 flags   bit 0x20: a 160-byte NUL-padded path follows the
//                     header; 0x04 is also seen. The list ends with a
//                     header whose type, offset and size are all zero.
// Entries without a path are written as NNNN_<namecrc>.<typecrc>.
//
// .img.ngc: 0x20-byte header, then one GX image.
//   +0x00 u16 0x0420
//   +0x0a u8 log2 width, +0x0b u8 log2 height
//   +0x0d u8 GX format (14 = CMPR on every file seen)
//   +0x10 u32 size of the top mip level, +0x14 u32 data offset (0x20)
// Only the top level is exported, as a TPL (which then decodes to PNG).
//
// Extract-only.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_NSGH_H
#define SZS_LIB_NSGH_H 1

#include "lib-nintendo.h"

enumError ScanNsPak (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);
enumError ScanNsImg (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size, ccp name);

#endif
