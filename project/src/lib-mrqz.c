// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "MRQZ" archive scanner; see lib-mrqz.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-mrqz.h"
#include "lib-nintendo.h"
#include <string.h>

#define MRQZ_MAX_ENTRIES 0x10000

static u32 mrqz_rd32 (const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

enumError ScanMrqz (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries)
{
	if (!f || !entries || !n_entries || file_size < 0x40)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	u8 h[0x10];
	if (fseeko (f, 0, SEEK_SET) || fread (h, 1, sizeof (h), f) != sizeof (h) || memcmp (h, "MRQZ", 4)
		|| (h[8] | h[9] << 8) != 2)
		return EINVAL;
	const u64 hdr = h[10] | h[11] << 8;
	const u32 count = mrqz_rd32 (h + 12);
	if ((hdr != 0x80 && hdr != 0x1000) || !count || count > MRQZ_MAX_ENTRIES)
		return EINVAL;
	const u64 table = hdr + 8, table_end = table + 0x4cull * count;
	const u64 base = (table_end + hdr - 1) / hdr * hdr;
	if (table_end > file_size || base > file_size)
		return EINVAL;

	u8 *tab = MALLOC (table_end - table + 1);
	stream_entry_t *out = CALLOC (count, sizeof (*out));
	if (!tab || !out || fseeko (f, table, SEEK_SET)
		|| fread (tab, 1, table_end - table, f) != table_end - table)
	{
		FREE (tab);
		FREE (out);
		return EINVAL;
	}

	uint n = 0;
	enumError res = ERR_OK;
	for (uint i = 0; i < count; i++)
	{
		const u8 *r = tab + 0x4cull * i;
		const u32 off = mrqz_rd32 (r + 64), size = mrqz_rd32 (r + 68);
		if (base + off + size > file_size || !memchr (r, 0, 64) || !r[0])
			continue;
		const char *s = (const char *)r;
		while (*s == '/')
			s++;
		char name[400];
		if (strlen (s) + 8 >= sizeof (name) || !OwnedNameOk (s))
			snprintf (name, sizeof (name), "%05u.bin", i);
		else
			snprintf (name, sizeof (name), "%s", s);
		for (uint j = 0; j < n; j++)
			if (!strcmp (out[j].name, name))
			{
				snprintf (name, sizeof (name), "%05u.bin", i);
				break;
			}
		if (!StreamEntryAdd (out, n, name, base + off, size))
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
