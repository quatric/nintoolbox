// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// h.a.n.d. FBC bundles; see lib-fbc.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-fbc.h"
#include <string.h>

#define FBC_MAX_FILES 0x10000

static u32 fbc_rd32 (const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

bool IsFBC (const u8 *d, size_t size)
{
	if (size < 0x60 || d[0] || d[1] != 0x14 || d[2] || d[3] != 6)
		return false;
	const u32 n = fbc_rd32 (d + 4);
	return n && n <= FBC_MAX_FILES && 0x40 + 4 * (u64)n <= size && 64 * (u64)n <= size;
}

enumError ScanFBC (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *d, size_t size)
{
	if (!entries || !n_entries || !IsFBC (d, size))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;
	const u32 n = fbc_rd32 (d + 4);
	const size_t sizes = size - 64 * (size_t)n, names = sizes + 32 * (size_t)n;
	nintendo_sarc_entry_t *out = CALLOC (n, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;
	uint cnt = 0;
	for (u32 i = 0; i < n; i++)
	{
		const u64 off = 0x40 + (u64)fbc_rd32 (d + 0x40 + 4 * i);
		const u32 sz = fbc_rd32 (d + sizes + 32 * i);
		if (off + sz > size)
			break;
		char name[40], path[64];
		snprintf (name, sizeof (name), "%.32s", (ccp)d + names + 32 * i);
		if (!name[0] || !OwnedNameOk (name))
			snprintf (name, sizeof (name), "%04u.bin", i);
		// the file system is case-insensitive: keep names unique
		snprintf (path, sizeof (path), "%s", name);
		for (uint dup = 1;; dup++)
		{
			bool clash = false;
			for (uint j = 0; j < cnt && !clash; j++)
				clash = !strcasecmp (out[j].name, path);
			if (!clash)
				break;
			const char *dot = strrchr (name, '.');
			if (dot)
				snprintf (path, sizeof (path), "%.*s_%u%s", (int)(dot - name), name, dup, dot);
			else
				snprintf (path, sizeof (path), "%s_%u", name, dup);
		}
		if (!OwnedEntryAdd (out, cnt, path, d + off, sz))
		{
			ResetOwnedEntries (out, cnt);
			return ERR_CANT_CREATE;
		}
		cnt++;
	}
	if (!cnt)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = cnt;
	return ERR_OK;
}
