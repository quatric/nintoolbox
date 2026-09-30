// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Harmonix "Ark" archives: a header (*.hdr) plus one or more data parts
// (<stem>_0.ark, <stem>_1.ark, ...). Used by Guitar Hero 1/2, Rock Band 1-3,
// The Beatles: Rock Band, AC/DC Live: Rock Band, Green Day: Rock Band and
// Lego Rock Band.
//
// The format is documented in code by PikminGuts92's Mackiloha / ArkHelper
// (Ark/ArkFile.cs, Crypt.cs); this is an independent C reimplementation,
// checked against the Wii AC/DC Live: Rock Band Track Pack disc, where the
// entry sizes of all 3418 members add up to the ark's length to the byte.
//
// Header encryption. If the first u32 is not a known version it is a key:
// every following byte is XORed with the low byte of a Lehmer/Park-Miller
// generator (key = key * 16807 mod 2^31-1, computed with Schrage's method as
// `k - (k/127773)*127773) * 16807 - (k/127773)*2836`, +0x7fffffff when <= 0)
// stepped once per byte. If the version after that is still unknown it is
// stored complemented and the rest of the header needs one more `^ 0xff`.
//
// Layout of the decrypted header, little-endian on the Wii:
//   u32 version                   2..7, 9, 10
//   [v>=6] u32 n; n*16 bytes      hashes, skipped
//   u32 part_count
//   u32 part_size_count           equals part_count
//   part sizes                    u32 each; 64-bit each in version 4 (a "broken"
//                                 v4 that is really v3/v5 is detected when the
//                                 last 64-bit size exceeds 32 bits, then they
//                                 are re-read as u32)
//   [v>=5 or broken v4] u32 n; n length-prefixed strings   part names, ignored
//                                 (parts are <hdr stem>_<i>.ark)
//   [6<=v<=9] u32 n; n*4 bytes    hashes, skipped
//   [v>=7] u32 n; n * (u32 m; m length-prefixed strings)   file lists, skipped
//   v<=7: u32 blob_size, NUL-terminated strings; u32 n, n*i32 string index;
//         u32 entries; each {offset (i64 for v>=4 and not broken v4, else
//         u32), i32 file string idx, i32 dir string idx, u32 size, u32
//         inflated_size (0 = stored)}; the indexes go through the index table
//   v>=9: u32 entries; each {i64 offset, u32-length string full path, i32
//         flag, u32 size, [v<=9] u32 unknown}; then u32 n; n*4 bytes hash
//         table
// Offsets are global across the parts; a member never straddles two parts.
//
// Extract-only.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_RBARK_H
#define SZS_LIB_RBARK_H 1

#include "lib-nintendo.h"

#define RBARK_MAX_PARTS 256

typedef struct rbark_entry_t
{
	char *path; // "dir/file", '/' separated
	u64 offset; // global offset over all parts
	u32 size;
	u32 inflated; // 0 = stored
} rbark_entry_t;

typedef struct rbark_t
{
	uint version;
	uint n_parts;
	u64 part_size[RBARK_MAX_PARTS];
	rbark_entry_t *e;
	uint n;
} rbark_t;

// True if 'hdr' decodes to a supported version with a self-consistent table.
bool IsRbArkHeader (const u8 *hdr, size_t size);
// Parses (decrypting a copy as needed) into 'a'; free with RbArkFree().
enumError RbArkParse (rbark_t *a, const u8 *hdr, size_t size);
void RbArkFree (rbark_t *a);
// Part index and offset inside that part for a global offset.
bool RbArkLocate (const rbark_t *a, u64 offset, uint *part, u64 *local);

#endif
