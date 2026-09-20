// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Chicken Shoot (Wii / PC) .dct stage and graphics archive format.
//
// .dct files contain:
//   - 16-byte header with stage/graphics configuration:
//       u16 version (5)
//       u16 num_bgr (number of background records)
//       u16 num_anim (number of animation sprite records)
//       u16 num_obj (number of objects in main parameter table)
//       u16 h4, h5, h6, h7 (auxiliary table dimensions)
//   - num_bgr 16-byte background records:
//       u16 unk0, width, height, flags, has_palette, aux_flag, unk6, unk7
//   - Background data:
//       If (flags & 1) && width * height > 0:
//         u32 compressed_size (little-endian)
//         compressed_size bytes of Chicken Shoot LZ-compressed CI8 pixel indices
//       If has_palette != 0:
//         512 bytes uncompressed RGB555 palette (256 x big-endian u16)
//       If aux_flag != 0:
//         u32 aux_compressed_size + aux_compressed_size bytes of compressed data
//   - Animation data:
//       num_anim animation records:
//         10-byte header: u16 id, width, height, frames, flags
//         128-byte null-terminated animation name string
//         If (flags & 1):
//           512 bytes uncompressed RGB555 palette (256 x big-endian u16)
//           If width * height * frames > 0:
//             u32 compressed_size (little-endian)
//             compressed_size bytes of Chicken Shoot LZ-compressed CI8 frames
//   - Object data:
//       Main object table:
//         u32 compressed_size + compressed_size bytes of compressed data
//         (decompresses to num_obj * 2 bytes of object parameters)
//       Auxiliary object tables:
//         h4 * h5 chunks, each u32 compressed_size + compressed data
//
// Chicken Shoot LZ compression algorithm:
//   Precomputed distance table: table[i] = (i * i) / 2 for i = 0..255; table[1] = 1.
//   Stream reading 2-byte commands (b0, b1):
//     If b0 == 0: literal run of b1 bytes follows directly in stream.
//     If b0 != 0: LZ backreference of length b1 from distance table[b0].
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_CS_DCT_H
#define SZS_LIB_CS_DCT_H 1

#include "lib-nintendo.h"
#include <stdio.h>

bool IsCSDCT (const u8 *data, size_t size);
enumError DecompressCS_LZ (const u8 *src, size_t src_size, u8 *dst, size_t dst_size, size_t *produced);
enumError CompressCS_LZ (const u8 *src, size_t src_size, u8 **out_data, size_t *out_size);
enumError DecodeCSDCT_Text (FILE *out, const u8 *data, size_t size);
enumError ExtractCSDCTArchive (ccp arg, ccp basedir, uint depth, const u8 *data, size_t size);

#endif // SZS_LIB_CS_DCT_H
