// SPDX-License-Identifier: GPL-2.0+
// Safecracker (Wii) bigfile table-of-contents format (.TOC, paired with .DAT).
//
// Confirmed byte-exact on both DATA/files/BIGFILE_BOOT_NGC.{TOC,DAT} (2048 +
// 720896 bytes) and structurally consistent with DATA/files/BIGFILE_FULL_NGC.
// {TOC,DAT} (606208 + 622018560 bytes): the .TOC is a small, little-endian,
// fixed-slot record table that indexes byte ranges inside the paired .DAT.
//
// Layout (all little-endian):
//
//   offset 0x00  u32  slot_count      total number of table slots (used +
//                                       unused sentinel slots)
//   offset 0x04  char magic[2]        "LE" (the "13 00 00 00 4c 45" pattern
//                                       noted during the initial disc scan)
//   offset 0x06  u16  reserved
//   offset 0x08  u32  unknown0        varies per file, not yet decoded
//   offset 0x0c  u32  unknown1        varies per file, not yet decoded
//   offset 0x10  u32  used_size       total bytes actually used in the .DAT
//                                       (== offset+size of the last real
//                                       entry), repeated 4x for redundancy
//   offset 0x20  u32  unknown2 (x2)   0 in both samples
//   offset 0x28  ...  slot[0 .. slot_count-1], SF_TOC_RECORD_SIZE bytes each
//
// Each slot (36 bytes / 9 u32 fields):
//
//   +0x00  u32  id            monotonically increasing per real entry
//   +0x04  u32  tag           constant 0xb067ebb2 for every real entry
//   +0x08  u32  size          byte length of this entry's data in the .DAT
//   +0x0c  u32  offset        byte offset of this entry's data in the .DAT;
//                               offset[n] == offset[n-1] + size[n-1], i.e.
//                               entries are laid out back-to-back in id order
//   +0x10  u32  offset_dup1   same as +0x0c (redundant copy)
//   +0x14  u32  offset_dup2   same as +0x0c (redundant copy)
//   +0x18  u32  offset_dup3   same as +0x0c (redundant copy)
//   +0x1c  u32  zero          0
//   +0x20  u32  zero2         0
//
// A slot past the last real entry (there is always at least one, since
// slot_count includes an unused sentinel) is filled entirely with the byte
// pattern 0xAA ("id" and "tag" both read as 0xAAAAAAAA) rather than zero;
// DecodeSafecrackerTOC() stops appending entries at the first such sentinel
// or the first entry whose tag isn't SF_TOC_TAG.
//
// The .DAT payload itself (per-entry sub-format) is NOT decoded here.
#ifndef LIB_SAFECRACKER_H
#define LIB_SAFECRACKER_H 1

#include "lib-std.h"

//-----------------------------------------------------------------------------

#define SF_TOC_HEADER_SIZE 0x28
#define SF_TOC_RECORD_SIZE 36
#define SF_TOC_MAGIC_OFF 4 // "LE"
#define SF_TOC_TAG 0xb067ebb2
#define SF_TOC_SENTINEL_U32 0xaaaaaaaa

typedef struct sf_toc_entry_t
{
	u32 id;
	u32 size;
	u32 offset; // into the paired .DAT
} sf_toc_entry_t;

typedef struct sf_toc_t
{
	u32 slot_count;
	u32 used_size; // total bytes used in the paired .DAT
	sf_toc_entry_t *entry;
	uint n;
	uint n_alloc;
} sf_toc_t;

//-----------------------------------------------------------------------------
// Magic/structural probe: slot_count field is sane, "LE" magic present at
// offset 4, and slot[0] (if slot_count > 0) carries the expected tag.
int IsSafecrackerTOC (const u8 *data, size_t size);

// Parses the header and every real (non-sentinel) slot. Returns ERR_OK and
// a populated, caller-owned table (free with FreeSafecrackerTOC) even if
// zero real entries were found; returns an error only if IsSafecrackerTOC()
// would already reject 'data'.
enumError DecodeSafecrackerTOC (sf_toc_t *toc, const u8 *data, size_t size);

void FreeSafecrackerTOC (sf_toc_t *toc);

// Text dump: header fields, then one line per entry (index, id, size,
// offset, offset+size).
enumError DecodeSafecrackerTOC_Text (FILE *f, const u8 *data, size_t size);

#endif // LIB_SAFECRACKER_H
