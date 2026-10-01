// SPDX-License-Identifier: GPL-2.0+
// Natsume "BIN\0" archives; see lib-natbin.h.
#include "lib-natbin.h"

#define NATBIN_MAX_COUNT 100000

static u32 nb_le32 (const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

static bool natbin_geometry (const u8 *d, size_t size, u32 *count)
{
	if (!d || size < 0x20 || memcmp (d, "BIN\0", 4) || d[4] != 1 || d[5] || nb_le32 (d + 12))
		return false;
	const u32 n = nb_le32 (d + 8);
	if (!n || n > NATBIN_MAX_COUNT || 0x10 + (u64)n * 16 > size)
		return false;
	for (u32 i = 0; i < n; i++)
	{
		const u8 *e = d + 0x10 + (size_t)i * 16;
		const u32 off = nb_le32 (e), sz = nb_le32 (e + 4);
		if (nb_le32 (e + 8) || nb_le32 (e + 12) || off < 0x10 + (u64)n * 16 || off > size || sz > size - off)
			return false;
	}
	*count = n;
	return true;
}

bool IsNatBin (const u8 *data, size_t size)
{
	u32 n;
	return natbin_geometry (data, size, &n);
}

enumError ScanNatBin (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *d, size_t size)
{
	u32 n;
	if (!entries || !n_entries || !natbin_geometry (d, size, &n))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	nintendo_sarc_entry_t *v = CALLOC (n, sizeof (*v));
	if (!v)
		return ERR_OUT_OF_MEMORY;
	uint made = 0;
	for (u32 i = 0; i < n; i++)
	{
		const u8 *e = d + 0x10 + (size_t)i * 16;
		const u32 off = nb_le32 (e), sz = nb_le32 (e + 4);
		char ext[8] = "bin";
		if (sz >= 4 && d[off] >= 'a' && d[off] <= 'z' && d[off + 1] >= 'a' && d[off + 1] <= 'z'
			&& d[off + 2] >= 'a' && d[off + 2] <= 'z' && !d[off + 3])
		{
			memcpy (ext, d + off, 3);
			ext[3] = 0;
		}
		u8 *out = MALLOC (sz ? sz : 1);
		if (!out)
			continue;
		memcpy (out, d + off, sz);
		char name[32];
		snprintf (name, sizeof (name), "%04u.%s", i, ext);
		v[made].name = STRDUP (name);
		v[made].data = out;
		v[made].size = sz;
		made++;
	}
	if (!made)
	{
		FREE (v);
		return ERR_NOTHING_TO_DO;
	}
	*entries = v;
	*n_entries = made;
	return ERR_OK;
}
