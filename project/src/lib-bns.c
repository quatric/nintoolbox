#include "lib-std.h"
#include "lib-bns.h"
#include <string.h>

void ResetBNS (bns_t *bns)
{
	if (!bns)
		return;
	FREE (bns->entries);
	memset (bns, 0, sizeof (*bns));
}

enumError ScanBNS (bns_t *bns, const u8 *data, uint size)
{
	if (!bns || !data || size < 16)
		return ERR_NOTHING_TO_DO;

	memset (bns, 0, sizeof (*bns));

	const u32 n = rd_be32 (data + 4);
	const u32 blk = rd_be32 (data + 8);
	const u32 zero = rd_be32 (data + 0xc);
	if (zero || !n || n > 0x1000000 || !blk || blk & (blk - 1))
		return ERR_NOTHING_TO_DO;

	const u64 table_size = 16 + (u64)n * 8;
	if (table_size > size)
		return ERR_NOTHING_TO_DO;

	bns_entry_t *entries = CALLOC (n, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	for (uint i = 0; i < n; i++)
	{
		const u8 *h = data + 16 + i * 8;
		const u64 off = (u64)rd_be32 (h) * blk;
		const u32 esz = rd_be32 (h + 4);
		if (off + esz > size)
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

	bns->data = data;
	bns->size = size;
	bns->entries = entries;
	bns->n_entries = n;
	return ERR_OK;
}
