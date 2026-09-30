// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Radical Entertainment "ATG CORE CEMENT LIBRARY" archives (*.rcf; Crash of
// the Titans, Wii). The older 1.2 "RADCORE CEMENT LIBRARY" used by The
// Simpsons: Hit & Run is a different layout and is not handled here.
//
// No public documentation covers this variant (the donut project's notes stop
// at the first 0x30 bytes). Worked out against the seven .rcf files of the
// Wii disc (danish, default, english, finnish, norway, sound, swedish; 953 to
// 6947 members each) and verified on all of them: every member lies inside the
// file, the member ranges tile the data area exactly with 0x800 alignment up
// to the last byte of the file, hashes are strictly ascending, and the
// extension of every name agrees with the magic of the member it lands on.
//
//   0x00 char[32]   "ATG CORE CEMENT LIBRARY", NUL padded
//   0x20 u8[4]      version 02 01 01 01
//   all remaining header and table fields are big-endian:
//   0x24 u32        table offset (0x3c)
//   0x28 u32        end of the first part of the table (not needed)
//   0x2c u32        names offset, also the start of the padding before the data
//   0x30 u32        names block length in bytes
//   0x34 u32        0
//   0x38 u32        entry count
//   table_offset:   count * {u32 name hash, u32 file offset, u32 size},
//                   sorted by hash; offsets are multiples of 0x800
//   names offset:   8 bytes {LE 0x800, 0}, then for each member in ascending
//                   *file offset* order, little-endian:
//                     u32 mtime (Unix), u32 0x800, u32 0, u32 name_len,
//                     char name[name_len] (backslash separated, NUL included),
//                     3 zero bytes
//                   The last record's padding fills out the block length.
// The i-th name belongs to the entry with the i-th smallest offset; the hash
// function itself is not needed and was not identified.
//
// Extract-only.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_RCF_H
#define SZS_LIB_RCF_H 1

#include "lib-nintendo.h"

typedef struct rcf_entry_t
{
	u32 hash, offset, size;
	char *name; // owned, '/' separated; NULL if the names block was unusable
	u32 mtime;
} rcf_entry_t;

typedef struct rcf_t
{
	uint n;
	rcf_entry_t *e; // in ascending file-offset order
} rcf_t;

// Bytes of header + table + names needed to parse; 0 if 'head' (at least 0x3c
// bytes) is not an ATG cement library.
size_t RcfHeadSize (const u8 *head, size_t size);
// 'd' holds RcfHeadSize() bytes, 'file_size' is the real file length.
enumError RcfParse (rcf_t *r, const u8 *d, size_t size, u64 file_size);
void RcfFree (rcf_t *r);

#endif
