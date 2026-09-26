// SPDX-License-Identifier: GPL-2.0+
// "Go West! A Lucky Luke Adventure" TATE media archive -- see
// lib-pak-tate.h for exactly what is and is not understood.

#include "lib-pak-tate.h"
#include "lib-nintendo.h"
#include <string.h>

#define TATE_ENTRY_HEADER_SIZE 0x80
#define TATE_MAX_ENTRIES 0x1000

//-----------------------------------------------------------------------------

bool IsPakTate (const u8 *data, size_t size)
{
	if (!data || size < TATE_ENTRY_HEADER_SIZE || memcmp (data, "TATE", 4))
		return false;
	const u32 total = rd_le32 (data + 4);
	return total == size;
}

enumError ScanPakTate (pak_tate_t *pak, const u8 *data, size_t size)
{
	if (!pak || !IsPakTate (data, size))
		return ERR_INVALID_DATA;
	memset (pak, 0, sizeof (*pak));

	const u32 declared_count = rd_le32 (data + 8);
	if (declared_count > TATE_MAX_ENTRIES)
		return ERR_INVALID_DATA;

	pak_tate_entry_t *entries = CALLOC (declared_count ? declared_count : 1, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	memcpy (pak->archive_name, data + 0x10, sizeof (pak->archive_name) - 1);

	size_t off = TATE_ENTRY_HEADER_SIZE; // first "item" entry
	uint n = 0;
	while (off < size)
	{
		const u8 *hdr = data + off;
		if (off + TATE_ENTRY_HEADER_SIZE > size || memcmp (hdr, "item", 4))
			break; // clean end-of-table (zero padding)
		if (n >= declared_count)
		{
			FREE (entries);
			return ERR_INVALID_DATA;
		}

		const u32 data_size = rd_le32 (hdr + 4);
		const size_t data_off = off + TATE_ENTRY_HEADER_SIZE;
		if ((u64)data_off + data_size > size)
		{
			FREE (entries);
			return ERR_INVALID_DATA;
		}

		pak_tate_entry_t *e = entries + n++;
		memcpy (e->name, hdr + 0x10, sizeof (e->name) - 1);
		e->name[sizeof (e->name) - 1] = 0;
		e->data_offset = (u32)data_off;
		e->data_size = data_size;

		const size_t data_end = data_off + data_size;
		off = (data_end + TATE_ENTRY_HEADER_SIZE - 1) & ~((size_t)TATE_ENTRY_HEADER_SIZE - 1);
		if (off > size)
		{
			FREE (entries);
			return ERR_INVALID_DATA;
		}
	}

	if (n != declared_count)
	{
		FREE (entries);
		return ERR_INVALID_DATA;
	}
	// remainder to end-of-file must be zero padding
	for (size_t p = off; p < size; p++)
		if (data[p])
		{
			FREE (entries);
			return ERR_INVALID_DATA;
		}

	pak->raw = data;
	pak->raw_size = size;
	pak->n_entries = n;
	pak->entries = entries;
	return ERR_OK;
}

void ResetPakTate (pak_tate_t *pak)
{
	if (!pak)
		return;
	FREE (pak->entries);
	memset (pak, 0, sizeof (*pak));
}

enumError DecodePakTate_Text (FILE *f, const u8 *data, size_t size)
{
	pak_tate_t pak;
	enumError err = ScanPakTate (&pak, data, size);
	if (err)
		return err;

	fprintf (f, "TATE media archive: \"%s\", %u entries, %zu bytes\n", pak.archive_name,
		pak.n_entries, pak.raw_size);
	for (uint i = 0; i < pak.n_entries; i++)
	{
		const pak_tate_entry_t *e = pak.entries + i;
		fprintf (f, "%4u.  off=0x%08x  size=0x%08x (%u)  %s\n", i, e->data_offset, e->data_size,
			e->data_size, e->name);
	}

	ResetPakTate (&pak);
	return ERR_OK;
}
