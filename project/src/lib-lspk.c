#include "lib-std.h"
#include "lib-lspk.h"
#include <string.h>

void ResetLSPK (lspk_t *pak)
{
	if (!pak)
		return;
	FREE (pak->entries);
	memset (pak, 0, sizeof (*pak));
}

enumError ScanLSPK (
	lspk_t *pak, const u8 *pkh_data, uint pkh_size, const u8 *pk_data, uint pk_size)
{
	if (!pak || !pkh_data || pkh_size < 4 || !pk_data)
		return ERR_NOTHING_TO_DO;

	memset (pak, 0, sizeof (*pak));

	const u32 n = rd_be32 (pkh_data);
	if (!n || n > 0x1000000)
		return ERR_NOTHING_TO_DO;

	const u64 table_size = (u64)pkh_size - 4;
	if (table_size % n)
		return ERR_NOTHING_TO_DO;
	const u32 row = (u32)(table_size / n);
	if (row != 16 && row != 24)
		return ERR_NOTHING_TO_DO;

	lspk_entry_t *entries = CALLOC (n, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	for (uint i = 0; i < n; i++)
	{
		const u8 *h = pkh_data + 4 + (u64)i * row;
		u32 hash, dec_size, com_size;
		u64 off;

		if (row == 16)
		{
			hash = rd_be32 (h);
			off = rd_be32 (h + 4);
			dec_size = rd_be32 (h + 8);
			com_size = rd_be32 (h + 0xc);
		}
		else
		{
			hash = rd_be32 (h);
			const u32 off_hi = rd_be32 (h + 8);
			const u32 off_lo = rd_be32 (h + 0xc);
			off = (u64)off_hi << 32 | off_lo;
			dec_size = rd_be32 (h + 0x10);
			com_size = rd_be32 (h + 0x14);
		}

		const u32 stored_size = com_size ? com_size : dec_size;
		if (off + stored_size > pk_size)
			continue;

		entries[i].data = pk_data + off;
		entries[i].size = stored_size;
		entries[i].dec_size = com_size ? dec_size : 0;
		entries[i].hash = hash;
	}

	for (uint i = 0; i < n; i++)
		if (!entries[i].data)
		{
			FREE (entries);
			return ERR_NOTHING_TO_DO;
		}

	pak->pk_data = pk_data;
	pak->pk_size = pk_size;
	pak->entries = entries;
	pak->n_entries = n;
	return ERR_OK;
}
