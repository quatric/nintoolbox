// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "PACK" v2 archive scanner; see lib-packv2.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-packv2.h"
#include "lib-nintendo.h"
#include <string.h>

#define P2_MAX_ENTRIES 0x100000
#define P2_REC 0x18

static u32 p2_rd32 (const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

enumError ScanPackV2 (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries)
{
	if (!f || !entries || !n_entries || file_size < 0x40)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	u8 h[0x10];
	if (fseeko (f, 0, SEEK_SET) || fread (h, 1, sizeof (h), f) != sizeof (h) || memcmp (h, "PACK", 4)
		|| p2_rd32 (h + 4) != 2)
		return EINVAL;
	const u32 count = p2_rd32 (h + 8), table_end = p2_rd32 (h + 12);
	if (!count || count > P2_MAX_ENTRIES || 0x10 + (u64)P2_REC * count > table_end || table_end > file_size)
		return EINVAL;

	// Read the whole header region (table plus names) once.
	u8 *hdr = MALLOC (table_end + 1);
	if (!hdr)
		return ERR_CANT_CREATE;
	if (fseeko (f, 0, SEEK_SET) || fread (hdr, 1, table_end, f) != table_end)
	{
		FREE (hdr);
		return EINVAL;
	}
	hdr[table_end] = 0;

	stream_entry_t *out = CALLOC (count, sizeof (*out));
	if (!out)
	{
		FREE (hdr);
		return ERR_CANT_CREATE;
	}
	uint n = 0;
	enumError res = ERR_OK;
	for (uint i = 0; i < count; i++)
	{
		const u8 *r = hdr + 0x10 + (size_t)P2_REC * i;
		const u32 size = p2_rd32 (r), size2 = p2_rd32 (r + 4), off = p2_rd32 (r + 8),
			  noff = p2_rd32 (r + 12);
		if ((u64)off + size > file_size || noff < 0x10 || noff >= table_end)
			continue;
		char name[400];
		const char *nm = (const char *)hdr + noff;
		if (strlen (nm) + 8 >= sizeof (name) || !OwnedNameOk (nm))
			snprintf (name, sizeof (name), "%05u.bin", i);
		else
			snprintf (name, sizeof (name), "%s", nm);
		if (size != size2)
			strcat (name, ".packed");
		for (uint j = 0; j < n; j++)
			if (!strcmp (out[j].name, name))
			{
				snprintf (name, sizeof (name), "%05u.bin", i);
				break;
			}
		if (!StreamEntryAdd (out, n, name, off, size))
		{
			res = ERR_CANT_CREATE;
			break;
		}
		n++;
	}
	FREE (hdr);
	if (res || !n)
	{
		FreeStreamEntries (out, n);
		return res ? res : EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}
