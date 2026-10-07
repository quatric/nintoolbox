// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "wii\0" resource archive scanner; see lib-wiiresarc.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-wiiresarc.h"
#include "lib-nintendo.h"
#include <string.h>

#define WA_MAX_ENTRIES 0x10000
#define WA_REC 0x20

static u32 wa_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

enumError ScanWiiResArc (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries)
{
	if (!f || !entries || !n_entries || file_size < 0x100)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	u8 h[0x20];
	if (fseeko (f, 0, SEEK_SET) || fread (h, 1, sizeof (h), f) != sizeof (h) || memcmp (h, "wii\0", 4)
		|| wa_rd32 (h + 4))
		return EINVAL;
	const u32 count = wa_rd32 (h + 12), names_off = wa_rd32 (h + 0x14), base = wa_rd32 (h + 0x18);
	if (!count || count > WA_MAX_ENTRIES || names_off < 0x24 + (u64)WA_REC * count
		|| names_off + 4ull * count > file_size)
		return EINVAL;
	const u64 recs_off = names_off - (u64)WA_REC * count;

	u8 *tab = MALLOC ((size_t)WA_REC * count + (size_t)4 * count);
	if (!tab)
		return ERR_CANT_CREATE;
	if (fseeko (f, (off_t)recs_off, SEEK_SET)
		|| fread (tab, 1, (size_t)WA_REC * count + (size_t)4 * count, f)
			!= (size_t)WA_REC * count + (size_t)4 * count)
	{
		FREE (tab);
		return EINVAL;
	}
	const u8 *name_offs = tab + (size_t)WA_REC * count;
	const bool relative = !wa_rd32 (tab + 12);

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
		const u8 *r = tab + (size_t)WA_REC * i;
		const u32 type = wa_rd32 (r + 4), size = wa_rd32 (r + 8), off = wa_rd32 (r + 12);
		const u64 abs = relative ? (u64)base + off : off;
		if (abs + size > file_size)
			continue;

		char stem[128] = "";
		const u32 noff = wa_rd32 (name_offs + 4 * i);
		if (noff && noff < file_size)
		{
			u8 raw[sizeof (stem)];
			const size_t want = noff + sizeof (raw) <= file_size ? sizeof (raw) : file_size - noff;
			if (!fseeko (f, (off_t)noff, SEEK_SET) && fread (raw, 1, want, f) == want)
			{
				size_t l = 0;
				while (l < want && raw[l] && l + 1 < sizeof (stem))
					l++;
				if (l < want && !raw[l])
				{
					memcpy (stem, raw, l);
					stem[l] = 0;
				}
			}
		}
		for (char *p = stem; *p; p++)
			if (*p == '/' || *p == '\\' || *p == ':' || (u8)*p < 0x20)
				*p = '_';
		char name[192];
		if (*stem)
			snprintf (name, sizeof (name), "%s.t%u", stem, type);
		else
			snprintf (name, sizeof (name), "%04u.t%u", i, type);
		if (!OwnedNameOk (name))
			snprintf (name, sizeof (name), "%04u.t%u", i, type);
		for (uint j = 0; j < n; j++)
			if (!strcmp (out[j].name, name))
			{
				snprintf (name, sizeof (name), "%04u.t%u", i, type);
				break;
			}
		if (!StreamEntryAdd (out, n, name, abs, size))
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
