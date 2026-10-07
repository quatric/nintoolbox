// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// 2XL Games "LPAK" resource pack (".PAK", SCORE International Baja 1000 on
// Wii: ANIMS.PAK, BAJAINF.PAK, PARTICLES.PAK, PERMTEX.PAK, ...). A RIFF
// container, little-endian, verified on four retail files (959 members):
//   0x00 u32 payload size, 0x04 "RIFF" u32 size "LPAK"
//   then one "LIST"/"LDAT" per member, holding the chunks
//     "dir " asset bookkeeping (crc and offsets), not needed to unpack
//     "file" u32 type hash, u32 unpacked size, u32 header size H, then
//            (H-12)/4 u32 block start offsets, then the data: a sequence of
//            zlib streams, each inflating to 0x4000 bytes (the last one
//            less), so (H-12)/4+1 blocks
//     "str " NUL-separated path components, the last one the file name
// The members come out inflated; ones without a zlib header are kept as is.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_LPAK_H
#define SZS_LIB_LPAK_H 1

#include "lib-nintendo.h"

enumError ScanLpak (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
