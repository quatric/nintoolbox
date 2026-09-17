// SPDX-License-Identifier: GPL-2.0+
#include "lib-hbdf.h"
#include "lib-archive-util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool IsHBDF (const u8 *data, uint size)
{
	if (!data || size < 8)
		return false;
	if (memcmp (data, "HBDF", 4) && memcmp (data, "HSDF", 4))
		return false;
	const u32 fsize = rd_le32 (data + 4);
	if (fsize < 8 || fsize > size)
		return false;
	return true;
}

enumError ScanHBDF (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || !IsHBDF (data, size))
		return ERR_INVALID_DATA;

	*entries = 0;
	*n_entries = 0;

	// Count blocks
	uint block_cnt = 0;
	uint pos = 8;
	const u32 total_size = rd_le32 (data + 4);
	const uint limit = total_size <= size ? total_size : size;

	while (pos + 8 <= limit)
	{
		const u32 bsize = rd_le32 (data + pos + 4);
		if (bsize < 8 || pos + bsize > limit)
			break;
		block_cnt++;
		pos += bsize;
	}

	if (!block_cnt)
		return ERR_NOTHING_TO_DO;

	nintendo_sarc_entry_t *out = CALLOC (block_cnt, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint out_cnt = 0;
	pos = 8;
	for (uint i = 0; i < block_cnt && pos + 8 <= limit; i++)
	{
		char tag[5];
		memcpy (tag, data + pos, 4);
		tag[4] = 0;

		const u32 bsize = rd_le32 (data + pos + 4);
		if (bsize < 8 || pos + bsize > limit)
			break;

		char name[64];
		snprintf (name, sizeof (name), "%02u_%s.bin", i, tag);
		OwnedEntryAdd (out, out_cnt++, name, data + pos, bsize);
		pos += bsize;
	}

	*entries = out;
	*n_entries = out_cnt;
	return ERR_OK;
}
