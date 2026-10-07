// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "FCAT" container scanner; see lib-fcat.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-fcat.h"
#include "lib-nintendo.h"
#include <string.h>

#define FCAT_MAX_ENTRIES 0x1000

static u32 fcat_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

enumError ScanFcat (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries)
{
	if (!f || !entries || !n_entries || file_size < 0x10)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	u8 h[8];
	if (fseeko (f, 0, SEEK_SET) || fread (h, 1, 8, f) != 8 || memcmp (h, "FCAT", 4))
		return EINVAL;
	const u32 count = fcat_rd32 (h + 4);
	if (!count || count > FCAT_MAX_ENTRIES || 8 + 8ull * count > file_size)
		return EINVAL;

	u8 *tab = MALLOC (8ull * count);
	stream_entry_t *out = CALLOC (count, sizeof (*out));
	if (!tab || !out || fread (tab, 1, 8ull * count, f) != 8ull * count)
	{
		FREE (tab);
		FREE (out);
		return EINVAL;
	}

	uint n = 0;
	enumError res = ERR_OK;
	for (uint i = 0; i < count; i++)
	{
		const u32 off = fcat_rd32 (tab + 8 * i), size = fcat_rd32 (tab + 8 * i + 4);
		if (!size || (u64)off + size > file_size)
			continue;
		u8 m[4] = {0};
		if (fseeko (f, off, SEEK_SET) || fread (m, 1, 4, f) != 4)
			continue;
		ccp ext = !memcmp (m, "bres", 4) ? "brres" : !memcmp (m, "U\xaa" "8-", 4) ? "arc"
			: !memcmp (m, "RARC", 4) ? "rarc" : !memcmp (m, "Yaz0", 4) ? "szs" : "bin";
		char name[32];
		snprintf (name, sizeof (name), "%03u.%s", i, ext);
		if (!StreamEntryAdd (out, n, name, off, size))
		{
			res = ERR_CANT_CREATE;
			break;
		}
		n++;
	}
	FREE (tab);
	if (res || !n)
	{
		FreeStreamEntries (out, n);
		return res ? res : EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}
