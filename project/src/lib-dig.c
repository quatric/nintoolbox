// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Hudson Soft/Racjin "cddata*.dig" streaming resource package (see
// lib-dig.h for the layout and the unsupported "type == 2" note).
//-----------------------------------------------------------------------------

#include "lib-std.h"
#include "lib-dig.h"
#include <string.h>

// The header proper is 12 bytes (type, unknown1, entry_hint), but the entry
// table starts 4 bytes later, at byte 16 -- the header is padded out to a
// 16-byte boundary before the first 16-byte entry record (confirmed: real
// samples have all-zero bytes 12..15, and entries only decode correctly,
// i.e. resolve to ranges that actually contain recognisable embedded
// content such as Nintendo TPL texture headers, when the table is read
// starting at byte 16).
#define DIG_HEADER_SIZE   12
#define DIG_TABLE_START   16
#define DIG_TABLE_SIZE    0x800   // entry table fills exactly one sector
#define DIG_ENTRY_SIZE    16
#define DIG_MAX_ENTRIES   ((DIG_TABLE_SIZE - DIG_TABLE_START) / DIG_ENTRY_SIZE)
#define DIG_SECTOR_SHIFT  11      // 1 sector == 2048 bytes == 1 << 11

bool IsDIG (const u8 *data, uint data_size, u64 real_size)
{
	if (!data || data_size < DIG_TABLE_SIZE || real_size < DIG_TABLE_SIZE)
		return false;

	uint n = 0;
	for (uint pos = DIG_TABLE_START; pos + DIG_ENTRY_SIZE <= DIG_TABLE_SIZE; pos += DIG_ENTRY_SIZE)
	{
		const u32 off_sect  = rd_be32 (data + pos);
		const u32 size_sect = rd_be32 (data + pos + 4);
		if (!off_sect && !size_sect)
			continue;

		const u64 off   = (u64)off_sect  << DIG_SECTOR_SHIFT;
		const u64 dsize = (u64)size_sect << DIG_SECTOR_SHIFT;
		if (!dsize || off >= real_size || off + dsize > real_size)
			return false; // one bad record => likely "type == 2", not this layout
		n++;
	}
	return n > 0;
}

enumError ScanDIG (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < DIG_TABLE_SIZE)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	nintendo_sarc_entry_t *out = CALLOC (DIG_MAX_ENTRIES, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint n = 0;
	for (uint pos = DIG_TABLE_START; pos + DIG_ENTRY_SIZE <= DIG_TABLE_SIZE; pos += DIG_ENTRY_SIZE)
	{
		const u32 off_sect  = rd_be32 (data + pos);
		const u32 size_sect = rd_be32 (data + pos + 4);
		if (!off_sect && !size_sect)
			continue; // unused slot

		const u64 off  = (u64)off_sect  << DIG_SECTOR_SHIFT;
		const u64 dsize = (u64)size_sect << DIG_SECTOR_SHIFT;
		if (!dsize || off >= size || off + dsize > size)
		{
			// A single bad record most likely means this file uses the
			// unsupported "type == 2" composite table, not this flat one;
			// bail out rather than emit a partial/garbage extraction.
			ResetOwnedEntries (out, n);
			return EINVAL;
		}

		char name[32];
		snprintf (name, sizeof (name), "entry_%04u.bin", n);
		if (!OwnedEntryAdd (out, n, name, data + off, (uint)dsize))
		{
			ResetOwnedEntries (out, n);
			return ERR_CANT_CREATE;
		}
		n++;
	}

	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}
