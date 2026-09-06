#include "lib-std.h"
#include "lib-gpak.h"
#include <string.h>

void ResetGPAK (gpak_t *pak)
{
	if (!pak)
		return;
	FREE (pak->entries);
	memset (pak, 0, sizeof (*pak));
}

enumError ScanGPAK (gpak_t *pak, const u8 *data, uint size)
{
	if (!pak || !data || size < 16)
		return ERR_NOTHING_TO_DO;

	memset (pak, 0, sizeof (*pak));

	const u32 n = rd_be32 (data);
	const u32 zero = rd_be32 (data + 8);
	if (zero || !n || n > 0x1000000)
		return ERR_NOTHING_TO_DO;

	const u64 table_size = (u64)(n + 1) * 16;
	if (table_size > size)
		return ERR_NOTHING_TO_DO;

	gpak_entry_t *entries = CALLOC (n, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	for (uint i = 0; i < n; i++)
	{
		const u8 *h = data + (i + 1) * 16;
		const u32 off = rd_be32 (h);
		const u32 esz = rd_be32 (h + 4);
		if ((u64)off + esz > size)
			continue;
		entries[i].data = data + off;
		entries[i].size = esz;
	}

	for (uint i = 0; i < n; i++)
		if (!entries[i].data)
		{
			FREE (entries);
			return ERR_NOTHING_TO_DO;
		}

	pak->data = data;
	pak->size = size;
	pak->entries = entries;
	pak->n_entries = n;
	return ERR_OK;
}

// Writer for the same nameless .pak: a 16-byte big-endian header holding the
// entry count, then one 16-byte (offset, size) record per member starting at
// 0x10, then the member data. The reader names members by ordinal
// ("file_%04u.bin"), so the writer takes them back in that order.
enumError CreateGPAK (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	const u32 data_start = (n_entries + 1) * 16;
	u32 total = data_start;
	for (uint i = 0; i < n_entries; i++)
		total = (total + entries[i].size + 15) & ~15u;

	u8 *buf = CALLOC (total, 1);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	wr_be32 (buf, n_entries);

	u32 off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		u8 *h = buf + (i + 1) * 16;
		wr_be32 (h, off);
		wr_be32 (h + 4, entries[i].size);
		if (entries[i].data && entries[i].size)
			memcpy (buf + off, entries[i].data, entries[i].size);
		off = (off + entries[i].size + 15) & ~15u;
	}

	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}
