// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// CiNG Wish Pack File (*.wpf, Hotel Dusk: Room 215 / Wish Room, Nintendo DS).
//
// The archive consists of consecutive 32-byte (0x20) header records for each member:
//   +0x00..+0x17: 24-byte null-terminated member filename (ASCII / Latin-1),
//                 typically prefixed by '\' (e.g. "\07-04L.bin", "\dusk.bin")
//   +0x18..+0x1B: u32 uncompressed member payload size (little-endian)
//   +0x1C..+0x1F: u32 next member offset in the archive (little-endian)
//   +0x20: payload bytes begin immediately (length = file_size)
//   Padding to 16-byte alignment precedes the next member.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_CINGWPF_H
#define SZS_LIB_CINGWPF_H 1

#include "lib-nintendo.h"

#define CINGWPF_HDR_SIZE 0x20

typedef struct cingwpf_entry_t
{
	char name[32];
	u32 offset;      // file offset where payload starts (entry_offset + 0x20)
	u32 size;        // payload size
	u32 next_offset; // next entry offset
} cingwpf_entry_t;

typedef struct cingwpf_t
{
	uint n;
	cingwpf_entry_t *e;
} cingwpf_t;

bool IsCingWpf (const u8 *head, size_t head_size, u64 file_size);
enumError CingWpfParse (cingwpf_t *wpf, const u8 *d, size_t size, u64 file_size);
void CingWpfFree (cingwpf_t *wpf);

#endif
