// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Sega "ARCB" archive (".arc"; Super Monkey Ball: Banana Blitz, 417 of its
// 426 archives) and the "AVLZ" compression it carries. Big-endian, verified
// on all 417 retail archives:
//   0x00 "ARCB", u32 version (0x03010000), u32 flags, u32 file_size,
//        u32 data_start, u32 sizes, u32 file_size, u32 0
//   0x20 a standard U8 archive ("U\xaa8-"), whose data_offset points at the
//        data block at 0x20 + data_offset. Node data offsets and sizes are
//        relative to the *unpacked* data block.
//   data block: "AVLZ", u32 unpacked_size, u32 packed_size (incl. the 12-byte
//        header), then the LZSS stream.
// AVLZ is Okumura LZSS: 4096-byte ring pre-filled with zeros, write position
// 0xfee, flag bytes read LSB first (1 = literal byte, 0 = 2-byte match
// {u8 pos_lo, u8 (pos_hi << 4) | (len - 3)}).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_ARCB_H
#define SZS_LIB_ARCB_H 1

#include "lib-nintendo.h"

// Decode an "AVLZ" block. *out is malloc'ed (free with FREE).
enumError DecodeAvlz (const u8 *src, size_t size, u8 **out, uint *out_size);

enumError ScanArcb (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
