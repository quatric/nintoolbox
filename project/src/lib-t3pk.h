// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Top Trumps - Doctor Who (Wii, Play It Games / Asylum Entertainment)
// "T3PK4.00" resource pack (.t3p; only one sample exists on the disc,
// DATA/files/packwii.t3p, 24797536 bytes). See lib-t3pk.c for the exact
// byte layout of what is confirmed.
//-----------------------------------------------------------------------------
#ifndef LIB_T3PK_H
#define LIB_T3PK_H 1

#include "lib-std.h"

// 8-byte ASCII magic "T3PK4.00" at offset 0. A short big-endian header
// follows (fields not all understood -- see lib-t3pk.c); the only header
// field trusted here is the u32 BE entry count at +0x08 (offset 8, NOT the
// first byte after the magic -- the magic itself is 8 bytes).
//
// The entry table starts at fixed offset 0x40 and is a run of fixed
// 40-byte (0x28) big-endian records:
//   u32 data_offset;  // absolute byte offset of this entry's data
//   u32 data_size;    // byte length of this entry's data
//   u32 field2;        // unknown (roughly correlates with a larger,
//                       // possibly "decompressed", size but no exact
//                       // formula was confirmed)
//   u32 field3;        // unknown, high-entropy (looks like a checksum)
//   u32 field4, field5; // small, slowly-increasing integers, meaning
//                        // unconfirmed (possibly a sequential asset id)
//   u32 field6;         // unknown, values seen: 0, 0x40004000, 0x10001000
//                        // (looks like two packed u16 values, maybe
//                        // format/dimension flags -- not confirmed)
//   u32 reserved[3];     // always zero in every record observed
// The table ends at the first record whose data_offset AND data_size are
// both zero (a terminator record); the confirmed header entry-count field
// equals (real entry count + 1) for that terminator.
//
// NOT understood: for most entries (about 85% in the one sample checked),
// data_offset + data_size exactly equals the next entry's data_offset
// (data is packed contiguously); for the remaining ~15% (all late in the
// table) there is a gap of up to several hundred KB between them, and the
// LAST entry's data_size field is always 0. There is also an unindexed gap
// of roughly 306 KB between the end of the entry table and the first
// entry's data_offset whose content/purpose is not identified. Because of
// this, data_size (not the gap to the next entry) is treated as the
// authoritative length of each entry's data, but it should not be assumed
// to account for every byte of the file.
typedef struct t3pk_entry_t
{
	u32 data_offset;
	u32 data_size;
	u32 field2, field3, field4, field5, field6;
} t3pk_entry_t;

typedef struct t3pk_t
{
	const u8 *raw;
	size_t raw_size;
	uint n_entries;
	t3pk_entry_t *entries;
} t3pk_t;

bool IsT3PK (const u8 *data, size_t size);
enumError ScanT3PK (t3pk_t *pk, const u8 *data, size_t size);
void ResetT3PK (t3pk_t *pk);
enumError DecodeT3PK_Text (FILE *f, const u8 *data, size_t size);

#endif // LIB_T3PK_H
