#include "lib-std.h"
#include "lib-nccarc.h"
#include <string.h>
#include <errno.h>

void ResetNCCARC (nccarc_t *nc)
{
	if (!nc)
		return;
	FREE (nc->entries);
	memset (nc, 0, sizeof (*nc));
}

enumError ScanNCCARC (nccarc_t *nc, const u8 *data, uint size)
{
	if (!nc || !data)
		return EINVAL;
	memset (nc, 0, sizeof (*nc));
	if (size < 8)
		return EINVAL;

	const u32 table_bytes = rd_le32 (data);
	if (!table_bytes || table_bytes & 3 || table_bytes > size)
		return EINVAL;
	const uint n = table_bytes / 4;
	if (n < 2 || n > 0x40000)
		return EINVAL;

	u32 *off = CALLOC (n, sizeof (*off));
	bool *flag = CALLOC (n, sizeof (*flag));
	if (!off || !flag)
	{
		FREE (off);
		FREE (flag);
		return ERR_CANT_CREATE;
	}

	u32 prev = 0;
	uint i;
	for (i = 0; i < n; i++)
	{
		const u32 raw = rd_le32 (data + i * 4);
		const u32 masked = raw & 0x7fffffff;
		if (masked < prev || masked > size)
			break;
		off[i] = masked;
		flag[i] = (raw & 0x80000000) != 0;
		prev = masked;
	}
	if (i != n || off[0] != table_bytes || off[n - 1] != size)
	{
		FREE (off);
		FREE (flag);
		return EINVAL;
	}

	nccarc_entry_t *entries = CALLOC (n - 1, sizeof (*entries));
	if (!entries)
	{
		FREE (off);
		FREE (flag);
		return ERR_CANT_CREATE;
	}
	for (i = 0; i < n - 1; i++)
	{
		entries[i].data = data + off[i];
		entries[i].size = off[i + 1] - off[i];
		entries[i].flag = flag[i];
	}
	FREE (off);
	FREE (flag);

	nc->data = data;
	nc->size = size;
	nc->entries = entries;
	nc->n_entries = n - 1;
	return ERR_OK;
}

enumError CreateNCCARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	const uint n_offsets = n_entries + 1;
	const uint table_size = n_offsets * 4;

	u64 total_size = table_size;
	for (uint i = 0; i < n_entries; i++)
		total_size += entries[i].size;

	if (total_size > NFMT_MAX_OUTPUT)
		return EFBIG;

	u8 *out = CALLOC (1, (size_t)total_size);
	if (!out)
		return ERR_CANT_CREATE;

	u32 cur_off = table_size;
	for (uint i = 0; i < n_entries; i++)
	{
		wr_le32 (out + i * 4, cur_off);
		if (entries[i].size > 0 && entries[i].data)
			memcpy (out + cur_off, entries[i].data, entries[i].size);
		cur_off += entries[i].size;
	}
	wr_le32 (out + n_entries * 4, cur_off);

	*dest = out;
	*dest_size = (uint)total_size;
	return ERR_OK;
}
