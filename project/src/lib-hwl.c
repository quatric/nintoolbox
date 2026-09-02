#include "lib-std.h"
#include "lib-hwl.h"
#include <string.h>
#include <errno.h>

//-----------------------------------------------------------------------------
///////////////	   Hyrule Warriors Legends (3DS) .idx/.bin	///////////////
//-----------------------------------------------------------------------------

// Layout ported from aluigi's public hyrule_warriors_legends.bms: the .idx
// is nothing but an array of {u32 size, u32 offset} little-endian records
// addressing the sibling .bin, with size==0 marking a hole.  There is no
// magic, no header and no name table, so the caller must gate on the
// filename pair; every structural constraint that *can* be checked is
// checked here (record alignment, in-bounds members, at least one live
// entry) so a same-named but unrelated .idx cannot produce garbage output.
enumError ScanHWLegends (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *idx,
	uint idx_size, const u8 *bin, uint bin_size)
{
	if (!entries || !n_entries || !idx || !bin)
		return EINVAL;
	if (!idx_size || idx_size % 8 || idx_size > 8u * 0x100000)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const uint files = idx_size / 8;
	uint live = 0;
	for (uint i = 0; i < files; i++)
	{
		const u32 fsize = rd_le32 (idx + i * 8);
		const u32 foff = rd_le32 (idx + i * 8 + 4);
		if (!fsize)
			continue;
		if ((u64)foff + fsize > bin_size)
			return EINVAL; // an out-of-range member means this is not an .idx
		live++;
	}
	if (!live)
		return EINVAL;

	nintendo_sarc_entry_t *out = CALLOC (live, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint n = 0;
	for (uint i = 0; i < files; i++)
	{
		const u32 fsize = rd_le32 (idx + i * 8);
		const u32 foff = rd_le32 (idx + i * 8 + 4);
		if (!fsize)
			continue;
		char name[32];
		snprintf (name, sizeof (name), "%05u.bin", i);
		if (!OwnedEntryAdd (out, n, name, bin + foff, fsize))
		{
			ResetOwnedEntries (out, n);
			return ERR_CANT_CREATE;
		}
		n++;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

enumError CreateHWLegends (u8 **dest_idx, uint *dest_idx_size, u8 **dest_bin, uint *dest_bin_size,
	const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest_idx || !dest_idx_size || !dest_bin || !dest_bin_size || !entries || !n_entries)
		return EINVAL;

	uint max_slot = n_entries;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = entries[i].name;
		if (name)
		{
			char *end = 0;
			unsigned long val = strtoul (name, &end, 10);
			if (end && end != name && val < 0x100000)
			{
				if (val + 1 > max_slot)
					max_slot = (uint)(val + 1);
			}
		}
	}

	const uint n_slots = max_slot;
	const uint idx_sz = n_slots * 8;
	u8 *idx_buf = CALLOC (1, idx_sz);
	if (!idx_buf)
		return ERR_CANT_CREATE;

	u64 bin_total = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		bin_total += entries[i].size;
		bin_total = (bin_total + 15) & ~15u;
	}

	if (bin_total > 0x7fffffff)
	{
		FREE (idx_buf);
		return EFBIG;
	}

	u8 *bin_buf = CALLOC (1, (size_t)bin_total);
	if (!bin_buf && bin_total > 0)
	{
		FREE (idx_buf);
		return ERR_CANT_CREATE;
	}

	u32 cur_bin_off = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		uint slot = i;
		ccp name = entries[i].name;
		if (name)
		{
			char *end = 0;
			unsigned long val = strtoul (name, &end, 10);
			if (end && end != name && val < n_slots)
				slot = (uint)val;
		}

		wr_le32 (idx_buf + slot * 8, entries[i].size);
		wr_le32 (idx_buf + slot * 8 + 4, cur_bin_off);

		if (entries[i].data && entries[i].size)
			memcpy (bin_buf + cur_bin_off, entries[i].data, entries[i].size);

		cur_bin_off += entries[i].size;
		cur_bin_off = (cur_bin_off + 15) & ~15u;
	}

	*dest_idx = idx_buf;
	*dest_idx_size = idx_sz;
	*dest_bin = bin_buf;
	*dest_bin_size = (uint)bin_total;
	return ERR_OK;
}

