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

static int compare_lspk_entries (const void *a, const void *b)
{
	const nintendo_sarc_entry_t *ea = (const nintendo_sarc_entry_t *)a;
	const nintendo_sarc_entry_t *eb = (const nintendo_sarc_entry_t *)b;
	ccp na = ea->name ? ea->name : "";
	ccp nb = eb->name ? eb->name : "";
	ccp sa = strrchr (na, '/');
	if (sa)
		na = sa + 1;
	ccp sb = strrchr (nb, '/');
	if (sb)
		nb = sb + 1;
	return strcmp (na, nb);
}

static u32 lspk_hash_name (ccp name)
{
	if (!name)
		return 0;
	ccp slash = strrchr (name, '/');
	if (slash)
		name = slash + 1;
	// Check if name is hex hash e.g. "12345678.bin" or "12345678"
	char *endptr = 0;
	u32 val = (u32)strtoul (name, &endptr, 16);
	if (endptr && (*endptr == '.' || *endptr == '\0') && (endptr - name) == 8)
		return val;
	// Fallback: simple djb2 / sdbm hash if arbitrary string
	u32 h = 5381;
	for (ccp p = name; *p && *p != '.'; p++)
		h = ((h << 5) + h) + (u8)*p;
	return h;
}

enumError CreateLSPKArchive (
	u8 **dest_pkh, uint *dest_pkh_size, u8 **dest_pk, uint *dest_pk_size,
	const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest_pkh || !dest_pkh_size || !dest_pk || !dest_pk_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_lspk_entries);

	// PKH table: 4 bytes entry count + 16 bytes per entry
	const uint pkh_sz = 4 + n_entries * 16;
	u8 *pkh = CALLOC (pkh_sz, 1);
	if (!pkh)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}
	wr_be32 (pkh, n_entries);

	// Compute PK size: aligned to 16 bytes per entry payload
	u32 cur_pk_off = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		cur_pk_off = (cur_pk_off + 15) & ~15;
		cur_pk_off += sorted[i].size;
	}
	const u32 pk_sz = (cur_pk_off + 15) & ~15;
	u8 *pk = CALLOC (pk_sz ? pk_sz : 16, 1);
	if (!pk)
	{
		FREE (pkh);
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	cur_pk_off = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		cur_pk_off = (cur_pk_off + 15) & ~15;
		const u32 hash = lspk_hash_name (sorted[i].name);
		const u32 off = cur_pk_off;
		const u32 size = sorted[i].size;

		u8 *h = pkh + 4 + i * 16;
		wr_be32 (h + 0, hash);
		wr_be32 (h + 4, off);
		wr_be32 (h + 8, size);  // dec_size
		wr_be32 (h + 12, 0);    // com_size (0 = uncompressed raw)

		if (sorted[i].data && size > 0)
			memcpy (pk + off, sorted[i].data, size);

		cur_pk_off += size;
	}

	FREE (sorted);
	*dest_pkh = pkh;
	*dest_pkh_size = pkh_sz;
	*dest_pk = pk;
	*dest_pk_size = pk_sz;
	return ERR_OK;
}
