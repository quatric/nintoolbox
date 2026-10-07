// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// THQ Australia "pack" archive scanner; see lib-thqpack.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-thqpack.h"
#include "lib-nintendo.h"
#include <string.h>

#define TP_MAX_ENTRIES 0x10000
#define TP_MAX_NAMES 0x100000

static u32 tp_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

enumError ScanThqPack (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries)
{
	if (!f || !entries || !n_entries || file_size < 0x18)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	u8 h[0x18];
	if (fseeko (f, 0, SEEK_SET) || fread (h, 1, sizeof (h), f) != sizeof (h) || memcmp (h, "pack", 4)
		|| tp_rd32 (h + 4) != 1)
		return EINVAL;
	const u32 names_size = tp_rd32 (h + 8), names_off = tp_rd32 (h + 0x10), count = tp_rd32 (h + 0x14);
	if (!count || count > TP_MAX_ENTRIES || !names_size || names_size > TP_MAX_NAMES
		|| names_off != 0x18 + 16 * count || (u64)names_off + names_size > file_size)
		return EINVAL;

	u8 *tab = MALLOC ((size_t)16 * count + names_size + 1);
	if (!tab)
		return ERR_CANT_CREATE;
	if (fread (tab, 1, (size_t)16 * count, f) != (size_t)16 * count
		|| fread (tab + (size_t)16 * count, 1, names_size, f) != names_size)
	{
		FREE (tab);
		return EINVAL;
	}
	char *names = (char *)tab + (size_t)16 * count;
	names[names_size] = 0;

	stream_entry_t *out = CALLOC (count, sizeof (*out));
	if (!out)
	{
		FREE (tab);
		return ERR_CANT_CREATE;
	}
	uint n = 0;
	enumError res = ERR_OK;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = tab + (size_t)16 * i;
		const u32 noff = tp_rd32 (e), off = tp_rd32 (e + 4), size = tp_rd32 (e + 8);
		if (noff >= names_size || (u64)off + size > file_size)
			continue;
		char name[64];
		const char *nm = names + noff;
		if (!OwnedNameOk (nm))
		{
			snprintf (name, sizeof (name), "%04u.bin", i);
			nm = name;
		}
		if (!StreamEntryAdd (out, n, nm, off, size))
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
