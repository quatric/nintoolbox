// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Ubisoft Jade BigFile (".bf", magic "BIG\0" or the XOR-obfuscated "BUG\0").
// Used by the Jade/LyN engine family: Beyond Good & Evil, Rayman Raving
// Rabbids 1/2/TV Party, Prince of Persia (Wii), Petz, TMNT, Rabbids Go Home,
// My Word Coach, ... Layout after the public Ray1Map reverse engineering and
// verified against Wii retail files (little-endian, version 44):
//
//   0x00 char[4] "BIG\0" / "BUG\0"
//   0x04 u32 version, max_file, max_dir, max_key, root,
//        i32 first_free_file, first_free_dir, u32 size_of_fat, num_fat
//   0x28 u32 universe_key
//   0x2c version >= 43: 44 bytes of engine keys (u32 + ten u32 keys)
//   then num_fat "fat" blocks, each:
//     0x18-byte header { max_file, max_dir, pos_fat, next_pos_fat,
//                        first_index, last_index }
//     size_of_fat * 8   file refs   { u32 offset, u32 key }
//     size_of_fat * N   file infos  (N = 0x7C for v >= 42, 0x54 for 34/37/38,
//                        else 0x58): { u32 length, i32 prev, i32 next,
//                        i32 parent_dir, u32 date, char name[0x40], u32 p4rev,
//                        [v >= 42: char hash[0x20], u32] }
//     size_of_fat * 0x54 dir infos { i32 first_file, first_sub, prev, next,
//                        parent, char name[0x40] }
//   File payloads sit at their offset as u32 size (low 31 bits) + data.
// "BUG\0" files XOR the header fields and the fat blocks with the key
// b3 98 cc 66 indexed by absolute file offset % 4; payloads are not XORed.
// Members are extracted as stored; no decompression is attempted.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_JADEBIG_H
#define SZS_LIB_JADEBIG_H 1

#include "lib-std.h"
#include <stdio.h>

typedef struct jadebig_entry_t
{
	char *name; // relative path, '/' separated, never escapes the root
	u64 offset; // offset of the payload (after its u32 size word)
	u32 size;   // payload size
} jadebig_entry_t;

// Cheap header sanity test for file-type detection.
bool IsJadeBigHeader (const u8 *data, size_t size);

// Read the index of the open file. Returns EINVAL if it is not a Jade BigFile.
enumError ScanJadeBig (FILE *f, u64 file_size, jadebig_entry_t **entries, uint *n_entries);
void FreeJadeBig (jadebig_entry_t *entries, uint n_entries);

#endif
