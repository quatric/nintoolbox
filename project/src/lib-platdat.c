// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// PlatinumGames "DAT" archive scanner; see lib-platdat.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-platdat.h"
#include "lib-nintendo.h"
#include <string.h>

#define PD_MAX_ENTRIES 0x10000

static u32 pd_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

enumError ScanPlatDat (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries)
{
	if (!f || !entries || !n_entries || file_size < 0x20)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	u8 h[0x1c];
	if (fseeko (f, 0, SEEK_SET) || fread (h, 1, sizeof (h), f) != sizeof (h) || memcmp (h, "DAT", 4))
		return EINVAL;
	const u32 count = pd_rd32 (h + 4);
	const u64 ot = pd_rd32 (h + 8), nt = pd_rd32 (h + 16), st = pd_rd32 (h + 20);
	if (!count || count > PD_MAX_ENTRIES || ot + 4ull * count > file_size
		|| st + 4ull * count > file_size || nt + 4 > file_size)
		return EINVAL;

	u8 lb[4];
	if (fseeko (f, nt, SEEK_SET) || fread (lb, 1, 4, f) != 4)
		return EINVAL;
	const u32 len = pd_rd32 (lb);
	if (!len || len > 256 || nt + 4 + (u64)len * count > file_size)
		return EINVAL;

	u8 *offs = MALLOC (4ull * count), *sizes = MALLOC (4ull * count);
	u8 *names = MALLOC ((u64)len * count + 1);
	stream_entry_t *out = CALLOC (count, sizeof (*out));
	if (!offs || !sizes || !names || !out || fseeko (f, ot, SEEK_SET)
		|| fread (offs, 1, 4ull * count, f) != 4ull * count || fseeko (f, st, SEEK_SET)
		|| fread (sizes, 1, 4ull * count, f) != 4ull * count || fseeko (f, nt + 4, SEEK_SET)
		|| fread (names, 1, (u64)len * count, f) != (u64)len * count)
	{
		FREE (offs);
		FREE (sizes);
		FREE (names);
		FREE (out);
		return EINVAL;
	}

	uint n = 0;
	enumError res = ERR_OK;
	for (uint i = 0; i < count; i++)
	{
		const u32 off = pd_rd32 (offs + 4 * i), size = pd_rd32 (sizes + 4 * i);
		if ((u64)off + size > file_size)
			continue;
		char raw[260], name[300];
		memcpy (raw, names + (u64)len * i, len);
		raw[len] = 0;
		if (!raw[0] || !OwnedNameOk (raw))
			snprintf (name, sizeof (name), "%05u.bin", i);
		else
			snprintf (name, sizeof (name), "%s", raw);
		for (uint j = 0; j < n; j++)
			if (!strcmp (out[j].name, name))
			{
				snprintf (name, sizeof (name), "%05u_%s", i, raw[0] ? raw : "x");
				break;
			}
		if (!StreamEntryAdd (out, n, name, off, size))
		{
			res = ERR_CANT_CREATE;
			break;
		}
		n++;
	}
	FREE (offs);
	FREE (sizes);
	FREE (names);
	if (res || !n)
	{
		FreeStreamEntries (out, n);
		return res ? res : EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}
