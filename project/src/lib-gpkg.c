#include "lib-std.h"
#include "lib-szs.h"
#include "lib-gpkg.h"
#include <string.h>

void ResetGPKG (gpkg_t *pkg)
{
	if (!pkg)
		return;
	FREE (pkg->entries);
	FREE (pkg->data);
	memset (pkg, 0, sizeof (*pkg));
}

enumError ScanGPKG (gpkg_t *pkg, const u8 *data, uint size)
{
	if (!pkg || !data || size < 2 || data[0] != 0x78)
		return ERR_NOTHING_TO_DO;

	memset (pkg, 0, sizeof (*pkg));
	u8 *dec = 0;
	uint dec_size = 0;
	if (DecodeZlibGrow (&dec, &dec_size, data, size) != ERR_OK || dec_size < 0x14)
	{
		FREE (dec);
		return ERR_NOTHING_TO_DO;
	}

	const u32 data_off = rd_be32 (dec + 4);
	const u32 zero = rd_be32 (dec + 8);
	const u32 n = rd_be32 (dec + 16);
	if (zero || !n || n > 0x100000 || data_off > dec_size)
	{
		FREE (dec);
		return ERR_NOTHING_TO_DO;
	}

	const u64 table_end = (u64)0x14 + (u64)n * 0x28;
	if (table_end > dec_size)
	{
		FREE (dec);
		return ERR_NOTHING_TO_DO;
	}

	const u32 base_off = dec_size - data_off;

	gpkg_entry_t *entries = CALLOC (n, sizeof (*entries));
	if (!entries)
	{
		FREE (dec);
		return ERR_CANT_CREATE;
	}

	uint valid = 0;
	uint off = 0x14;
	for (uint i = 0; i < n; i++, off += 0x28)
	{
		const u8 *h = dec + off;
		memcpy (entries[i].name, h, 0x20);
		entries[i].name[0x20] = 0;

		const u32 entry_off = rd_be32 (h + 0x20);
		const u32 entry_size = rd_be32 (h + 0x24);
		const u64 real_off = (u64)entry_off + base_off;
		if (real_off + entry_size > dec_size)
			continue;

		entries[i].data = dec + real_off;
		entries[i].size = entry_size;
		valid++;
	}

	if (!valid)
	{
		FREE (entries);
		FREE (dec);
		return ERR_NOTHING_TO_DO;
	}

	pkg->data = dec;
	pkg->size = dec_size;
	pkg->entries = entries;
	pkg->n_entries = n;
	return ERR_OK;
}
