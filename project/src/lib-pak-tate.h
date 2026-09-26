// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "Go West! A Lucky Luke Adventure" (Wii) "TATE" media archive (.PAK, found
// as DATA/files/media/*.PAK on the retail EU disc). See lib-pak-tate.c for
// the exact byte layout, cross-checked byte-exact across three real sample
// files (krokodyle.PAK, canyon.PAK, box.PAK) of very different sizes.
//-----------------------------------------------------------------------------
#ifndef LIB_PAK_TATE_H
#define LIB_PAK_TATE_H 1

#include "lib-std.h"

//-----------------------------------------------------------------------------
// Fixed 0x80-byte (128-byte) little-endian entry header, repeated back to
// back with no gap: tag[4] ("TATE" for the single archive-root entry at
// offset 0, "item" for every following entry), u32 data_size (byte count of
// the data that immediately follows this 0x80-byte header -- for the root
// entry this equals the whole file size), u32 zero, u32 zero, then an
// ASCII/NUL-padded name (root: short archive name; item: full in-game
// backslash path) filling the rest of the 128 bytes from offset +0x10.
//
// Each entry's data occupies [entry_start+0x80, entry_start+0x80+data_size)
// and the next entry starts at that end address rounded UP to the next
// 0x80 boundary (zero-padding fills the gap). Confirmed exactly: walking
// the table this way from the root entry visits exactly root.item_count
// "item" entries in every sample file, and the walk ends precisely at a
// run of zero bytes that pads out to the declared total file size.
//
// For "item" entries, the data blob itself begins with one more 0x80-byte
// sub-header (tag "tate", lowercase) whose own fields are NOT decoded here
// (no consistent meaning was found relating them to the item's data_size
// or to the payload's own file format, which varies by name extension --
// .ebt, etc). Only the outer table (name, absolute data offset incl. that
// sub-header, and total data length) is exposed.
typedef struct pak_tate_entry_t
{
	char name[113]; // from +0x10, NUL-padded region is 0x70 bytes
	u32 data_offset; // absolute offset of this entry's data (starts
					 // with the undeciphered "tate" sub-header)
	u32 data_size; // byte length of that data
} pak_tate_entry_t;

typedef struct pak_tate_t
{
	const u8 *raw;
	size_t raw_size;
	char archive_name[113];
	uint n_entries;
	pak_tate_entry_t *entries;
} pak_tate_t;

// Detection: magic "TATE" at offset 0, plausible header, and the declared
// total-size field (u32 LE at +0x04) equal to the real file size.
bool IsPakTate (const u8 *data, size_t size);

// Walk the whole entry table; returns ERR_INVALID_DATA if the walk does not
// cleanly terminate at end-of-file with the declared entry count.
enumError ScanPakTate (pak_tate_t *pak, const u8 *data, size_t size);
void ResetPakTate (pak_tate_t *pak);

// Human-readable listing (index, offset, size, name) -- no payload decode.
enumError DecodePakTate_Text (FILE *f, const u8 *data, size_t size);

#endif // LIB_PAK_TATE_H
