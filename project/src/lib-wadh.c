// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Data Design Interactive WADH archive scanner; see lib-wadh.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-wadh.h"
#include <string.h>

#define WADH_NONE 0xffffffffu
#define WADH_MAX_ENTRIES 0x100000
#define WADH_MAX_DEPTH 64
#define WADH_MAX_NAME 512

static u32 wadh_rd32 (const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }

// Build "dir/dir/name" for entry idx by walking the parent table.
static bool wadh_path (char *dest, size_t dest_size, const u32 *parent, const u8 *dir,
	const char *names, u32 names_size, uint idx)
{
	ccp parts[WADH_MAX_DEPTH];
	uint np = 0;
	for (uint cur = idx; cur != 0 && cur != WADH_NONE; cur = parent[cur])
	{
		if (np == WADH_MAX_DEPTH)
			return false;
		const u32 noff = wadh_rd32 (dir + 32 * (size_t)cur);
		if (noff >= names_size)
			return false;
		parts[np++] = names + noff;
	}
	size_t pos = 0;
	while (np--)
	{
		const size_t l = strlen (parts[np]);
		if (pos + l + 2 > dest_size)
			return false;
		memcpy (dest + pos, parts[np], l);
		pos += l;
		dest[pos++] = '/';
	}
	if (pos)
		dest[pos - 1] = 0;
	else
		return false;
	return true;
}

enumError ScanWADH (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < 0x30 || memcmp (data, "WADH", 4))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 base = wadh_rd32 (data + 4);
	const u32 count = wadh_rd32 (data + 8);
	if (count < 2 || count > WADH_MAX_ENTRIES || base > size)
		return EINVAL;
	const u64 dir_end = 0x10 + 32ull * count;
	if (dir_end > base)
		return EINVAL;
	const u8 *dir = data + 0x10;
	const char *names = (const char *)data + dir_end;
	const u32 names_size = base - (u32)dir_end;
	if (!names_size || names[names_size - 1] != 0)
	{
		// The recorded table may be followed by padding; the last name still
		// has to be terminated inside it.
		if (!names_size || !memchr (names, 0, names_size))
			return EINVAL;
	}

	u32 *parent = CALLOC (count, sizeof (*parent));
	if (!parent)
		return ERR_CANT_CREATE;
	for (uint i = 0; i < count; i++)
		parent[i] = WADH_NONE;

	// Directory children hang off last_child / prev_sibling chains.
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = dir + 32 * (size_t)i;
		const u32 size_f = wadh_rd32 (e + 12);
		const u32 last = wadh_rd32 (e + 24);
		if (size_f || last == WADH_NONE)
			continue;
		uint guard = 0;
		for (u32 c = last; c != WADH_NONE; c = wadh_rd32 (dir + 32 * (size_t)c + 28))
		{
			if (c >= count || ++guard > count || parent[c] != WADH_NONE)
			{
				FREE (parent);
				return EINVAL;
			}
			parent[c] = i;
		}
	}

	nintendo_sarc_entry_t *out = CALLOC (count, sizeof (*out));
	if (!out)
	{
		FREE (parent);
		return ERR_CANT_CREATE;
	}

	uint n = 0;
	for (uint i = 1; i < count; i++)
	{
		const u8 *e = dir + 32 * (size_t)i;
		const u32 off = wadh_rd32 (e + 8);
		const u32 fsize = wadh_rd32 (e + 12);
		const u32 last = wadh_rd32 (e + 24);
		if (!fsize && last != WADH_NONE)
			continue; // directory
		if (parent[i] == WADH_NONE)
			continue;
		if ((u64)base + off + fsize > size)
		{
			ResetOwnedEntries (out, n);
			FREE (parent);
			return EINVAL;
		}
		char name[WADH_MAX_NAME];
		if (!wadh_path (name, sizeof (name), parent, dir, names, names_size, i)
			|| !OwnedNameOk (name))
			snprintf (name, sizeof (name), "%04u.bin", i);
		// Duplicate paths (e.g. two empty cache.dir stubs): keep both.
		for (uint k = 0; k < n; k++)
			if (!strcmp (out[k].name, name))
			{
				const size_t l = strlen (name);
				snprintf (name + (l + 12 < sizeof (name) ? l : sizeof (name) - 12), 12, ".%u", i);
				break;
			}
		if (!OwnedEntryAdd (out, n, name, data + base + off, fsize))
		{
			ResetOwnedEntries (out, n);
			FREE (parent);
			return ERR_CANT_CREATE;
		}
		n++;
	}
	FREE (parent);
	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}
