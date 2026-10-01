// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Capcom CPAC multi-section archive container (*.bin; Ghost Trick: Phantom
// Detective, Resident Evil: Deadly Silence, Ace Attorney series on Nintendo DS).
//
// The archive consists of an outer section index table followed by multiple
// self-contained sections:
//   Outer header at 0x00:
//     u32 header_len (e.g. 0x20 or 0x30; always a multiple of 8, >= 0x20)
//     Section table:
//       Entry 0: offset = header_len, size = u32 at +0x04
//       Entry i: offset = u32 at +(i*8), size = u32 at +(i*8+4)
//
//   Each section starts with a 24-byte tag header:
//     +0x00: u32 header_size (24)
//     +0x04: u32 version (2)
//     +0x08: u32 key_tag ('BKEY' = 0x424B4559 or 'PKEY' = 0x504B4559)
//     +0x0c: u32 next_chunk_off (24)
//     +0x10: u32 dat_tag ('BDAT' = 0x42444154 or 'PDAT' = 0x50444154)
//     +0x14: u32 table_size
//
//   BKEY / BDAT sections (3D models, 2D animations, textures, scripts):
//     - 32-byte section header
//     - Followed by 16-byte member records:
//         u32 off1, size1
//         u32 off2, size2
//     - Real payload start is section_off + table_size.
//     - High bit (0x80000000) of size indicates Nintendo LZ11 compression.
//     - Offsets and sizes are masked with 0x7FFFFFFF.
//
//   PKEY / PDAT sections (Palettes):
//     - 32-byte section header
//     - Followed by 4-byte palette descriptors:
//         u16 color_count_flag (0x0100 for 256 colors / 512 bytes, else 16 colors / 32 bytes)
//         u16 palette_bank_index
//     - Palette offset = payload_start + bank_index * 32.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_CPAC_H
#define SZS_LIB_CPAC_H 1

#include "lib-nintendo.h"

typedef struct cpac_entry_t
{
	u32 offset;
	u32 size;
	u16 section;
	u16 subindex;
	bool is_palette;
	bool is_lz11;
} cpac_entry_t;

typedef struct cpac_t
{
	uint n;
	cpac_entry_t *e;
} cpac_t;

bool IsCpac (const u8 *head, size_t head_size, u64 file_size);
size_t CpacHeadSize (const u8 *head, size_t size);
enumError CpacParse (cpac_t *cpac, const u8 *d, size_t size, u64 file_size);
void CpacFree (cpac_t *cpac);

#endif
