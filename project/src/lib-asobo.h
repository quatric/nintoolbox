// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Asobo Studio "Internal Cross Technology" BigFile volumes (Ratatouille, Wii
// .DRV). Layout after widberg/bff (BigFile v1.06.63), Wii = big-endian:
//   0x000 version string, e.g. "v1.06.63.01 - Asobo Studio - Internal Cross
//         Technology", zero padded to 0x100
//   0x100 u32 type, u32 n_blocks, u32 buffer_even, u32 buffer_odd,
//         u32 padded_size, u32 version[3]
//   0x120 n_blocks * {u32 n_resources, u32 padded_size, u32 data_size,
//         u32 working_buffer_offset, u32 first_name, u32 checksum}
//   0x800 blocks, back to back, each padded to `padded_size`. A block holds
//         n_resources resources:
//           u32 data_size (link header + stored body), u32 link_header_size,
//           u32 decompressed_size, u32 compressed_size (0 = stored),
//           u32 class_hash, u32 name_hash, link header, body
//         A compressed body is "u32le decomp, u32le comp (incl. these 8
//         bytes)" followed by an LZRS stream: big-endian u32 flag words, low
//         two bits = length shift, 30 items per word, top bit set = u16 match
//         (offset = (v & mask) + 1, length = (v >> (14 - shift)) + 3) else a
//         literal byte.
// Names are 32-bit hashes; class names are hashed with the Asobo CRC (MSB
// first table, LSB-first update, lower-cased input).
// Resources are written as <name hash>.<Class_Z or class hash>; a link
// header, when present, goes to <name hash>.<class>.lnk.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_ASOBO_H
#define SZS_LIB_ASOBO_H 1

#include "lib-nintendo.h"

// Bitmap_Z body: u32 width, u32 height, u32 size, u8 format (7 = RGB565,
// 12 = RGBA8, 14 = CMPR, GX tiled), u8 copy, u8 palette, u8 transp, u8 mips,
// u8 4, u16 flags, then the pixel data. Only the first level is decoded.
bool IsAsoboBitmap (const u8 *data, size_t size);
enumError DecodeAsoboBitmap (u8 **rgba, uint *width, uint *height, const u8 *data, size_t size);

enumError ScanAsoboDrv (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
