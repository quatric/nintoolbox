// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Level-5 / Armor Project PAC archive container (*.pac, *.dat; Dragon Quest IX
// on Nintendo DS).
//
// The archive consists of consecutive 80-byte (0x50) header records for each member:
//   Header at entry offset:
//     +0x00..+0x3f: null-terminated entry filename (up to ~36 bytes ASCII / Shift-JIS)
//                   followed by memory/tool scratch metadata
//     +0x40: u32 header_length (always 0x50 / 80)
//     +0x44: u32 file_size     (uncompressed member payload size in bytes)
//     +0x48: u32 alloc_size    (chunk allocation stride, typically 16-byte aligned)
//     +0x4c: u32 flags/magic   (engine flags / scratch address)
//     +0x50: payload begins immediately at entry_offset + 0x50.
//
// An archive may terminate when:
//   - file_size == 0xffffffff or alloc_size == 0xffffffff (sentinel record)
//   - header_length != 0x50 or alloc_size == 0
//   - entry name is empty or trailing padding bytes reach end of archive
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_L5PAC_H
#define SZS_LIB_L5PAC_H 1

#include "lib-nintendo.h"

#define L5PAC_HDR_SIZE 0x50

typedef struct l5pac_entry_t
{
	char name[64];
	u32 offset;      // file offset where member payload starts (entry_offset + 0x50)
	u32 size;        // payload size
	u32 alloc_size;  // chunk allocation stride
} l5pac_entry_t;

typedef struct l5pac_t
{
	uint n;
	l5pac_entry_t *e;
} l5pac_t;

bool IsL5Pac (const u8 *head, size_t head_size, u64 file_size);
enumError L5PacParse (l5pac_t *pac, const u8 *d, size_t size, u64 file_size);
void L5PacFree (l5pac_t *pac);

#endif
