// SPDX-License-Identifier: GPL-2.0+
// Town Factory "PCKG" packages; see lib-pckg.h.
#include "lib-pckg.h"

#define PCKG_MAX_ENTRIES 100000

static u32 pk_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

// Walk the entry chain; returns the entry count, or 0 if it is not a clean chain.
static uint pckg_walk (const u8 *d, size_t size, nintendo_sarc_entry_t *v)
{
	if (!d || size < 0x40 || memcmp (d, "PCKG", 4))
		return 0;
	uint n = 0;
	u64 e = 0x20;
	for (;;)
	{
		if (n >= PCKG_MAX_ENTRIES || e + 0x20 > size)
			return 0;
		const u32 next = pk_be32 (d + e), sz = pk_be32 (d + e + 4), doff = pk_be32 (d + e + 8);
		if (doff < 0x20 || e + doff > size || sz > size - e - doff)
			return 0;
		if (v)
		{
			char name[24];
			memcpy (name, d + e + 12, 20);
			name[20] = 0;
			for (char *c = name; *c; c++)
				if (*c == '/' || *c == '\\' || (unsigned char)*c < 0x20)
					*c = '_';
			if (!name[0] || !strcmp (name, ".") || !strcmp (name, ".."))
				snprintf (name, sizeof (name), "entry%u", n);
			v[n].name = STRDUP (name);
			v[n].size = sz;
			u8 *out = MALLOC (sz ? sz : 1);
			if (!out || !v[n].name)
				return 0;
			memcpy (out, d + e + doff, sz);
			v[n].data = out;
		}
		n++;
		if (!next)
		{
			const u64 end = e + doff + sz;
			return end <= size && size - end < 0x20 ? n : 0;
		}
		if (next < doff || e + next > size)
			return 0;
		e += next;
	}
}

bool IsPckg (const u8 *data, size_t size)
{
	return pckg_walk (data, size, 0) != 0;
}

enumError ScanPckg (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *d, size_t size)
{
	const uint n = pckg_walk (d, size, 0);
	if (!entries || !n_entries || !n)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;
	nintendo_sarc_entry_t *v = CALLOC (n, sizeof (*v));
	if (!v)
		return ERR_OUT_OF_MEMORY;
	if (pckg_walk (d, size, v) != n)
	{
		for (uint i = 0; i < n; i++)
		{
			FREE ((void *)v[i].name);
			FREE ((void *)v[i].data);
		}
		FREE (v);
		return ERR_WARNING;
	}
	// duplicate names -> name_dupN.ext
	for (uint i = 1; i < n; i++)
		for (uint j = 0, k = 2; j < i; j++)
			if (!strcmp (v[i].name, v[j].name))
			{
				char buf[64];
				const char *dot = strrchr (v[i].name, '.');
				const int stem = dot ? (int)(dot - v[i].name) : (int)strlen (v[i].name);
				snprintf (buf, sizeof (buf), "%.*s_dup%u%s", stem, v[i].name, k++, dot ? dot : "");
				FREE ((void *)v[i].name);
				v[i].name = STRDUP (buf);
				j = (uint)-1;
			}
	*entries = v;
	*n_entries = n;
	return ERR_OK;
}
