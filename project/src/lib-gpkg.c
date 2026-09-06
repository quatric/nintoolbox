#include "lib-std.h"
#include "lib-szs.h"
#include "lib-gpkg.h"
#include <string.h>
#include <zlib.h>

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

// Gorilla Games ".pkg" package writer (Bonsai Barber and this studio's
// other WiiWare titles).
//
// The whole package is one zlib stream. Inside it a 0x14-byte big-endian
// header is followed by 0x28-byte entries carrying a 0x20-byte name, an
// offset and a size. Entry offsets are relative to (decompressed size -
// data_off), so the writer picks data_off such that that base lands exactly
// on the first member.
//
// The zlib level is pinned so the same members always compress to the same
// bytes and a rebuild reproduces the package exactly.
enumError CreateGPKG (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	const u32 table_end = 0x14 + n_entries * 0x28;
	const u32 data_start = (table_end + 31) & ~31u;

	u32 total = data_start;
	for (uint i = 0; i < n_entries; i++)
		total = (total + entries[i].size + 31) & ~31u;

	u8 *plain = CALLOC (total, 1);
	if (!plain)
		return ERR_OUT_OF_MEMORY;

	wr_be32 (plain + 4, total - data_start); // data_off: base_off == data_start
	wr_be32 (plain + 16, n_entries);

	u32 off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = entries[i].name ? entries[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;

		u8 *h = plain + 0x14 + i * 0x28;
		strncpy ((char *)h, name, 0x20);
		wr_be32 (h + 0x20, off - data_start);
		wr_be32 (h + 0x24, entries[i].size);

		if (entries[i].data && entries[i].size)
			memcpy (plain + off, entries[i].data, entries[i].size);
		off = (off + entries[i].size + 31) & ~31u;
	}

	uLongf bound = compressBound (total);
	u8 *comp = MALLOC (bound);
	if (!comp)
	{
		FREE (plain);
		return ERR_OUT_OF_MEMORY;
	}
	if (compress2 (comp, &bound, plain, total, 9) != Z_OK)
	{
		FREE (plain);
		FREE (comp);
		return ERR_CANT_CREATE;
	}
	FREE (plain);

	*dest = comp;
	*dest_size = (uint)bound;
	return ERR_OK;
}
