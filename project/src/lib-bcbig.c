// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Blue Castle Games ".big" archives; see lib-bcbig.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-bcbig.h"
#include "lib-std.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define BCBIG_MAGIC 0x01020304u
#define BCBIG_ENTRY 20
#define BCBIG_MAX_ENTRIES 0x100000u
#define BCBIG_MAX_TOTAL 0x60000000ull

static u32 bb_le32 (const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

typedef struct bcbig_t
{
	uint count;
	u32 table;
	u32 names;
} bcbig_t;

static bool bcbig_geometry (bcbig_t *b, const u8 *d, size_t size)
{
	if (!d || size < 0x18 || bb_le32 (d) != BCBIG_MAGIC)
		return false;
	const u32 data_start = bb_le32 (d + 4), total = bb_le32 (d + 8), count = bb_le32 (d + 12),
			  table = bb_le32 (d + 16), names = bb_le32 (d + 20);
	if (total != size || !count || count > BCBIG_MAX_ENTRIES || table < 0x18)
		return false;
	if ((u64)table + (u64)count * BCBIG_ENTRY > names || names > data_start || data_start > size)
		return false;
	b->count = count;
	b->table = table;
	b->names = names;
	return true;
}

bool IsBcBig (const u8 *data, size_t size)
{
	bcbig_t b;
	return bcbig_geometry (&b, data, size);
}

// Case-insensitive path set: names can repeat (or differ only in case) and
// the host file system may fold case, so later duplicates get "_dupN".
typedef struct bcbig_seen_t
{
	char **slot;
	uint mask;
} bcbig_seen_t;

static uint bcbig_hash (ccp s)
{
	uint h = 2166136261u;
	for (; *s; s++)
		h = (h ^ (u8)((*s >= 'A' && *s <= 'Z') ? *s + 32 : *s)) * 16777619u;
	return h;
}

static bool bcbig_claim (bcbig_seen_t *t, ccp path)
{
	uint i = bcbig_hash (path) & t->mask;
	while (t->slot[i])
	{
		if (!strcasecmp (t->slot[i], path))
			return false;
		i = (i + 1) & t->mask;
	}
	t->slot[i] = STRDUP (path);
	return true;
}

enumError ScanBcBig (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *d, size_t size)
{
	bcbig_t b;
	if (!entries || !n_entries || !bcbig_geometry (&b, d, size))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	nintendo_sarc_entry_t *v = CALLOC (b.count, sizeof (*v));
	if (!v)
		return ERR_OUT_OF_MEMORY;
	bcbig_seen_t seen;
	uint cap = 16;
	while (cap < b.count * 2)
		cap <<= 1;
	seen.mask = cap - 1;
	seen.slot = CALLOC (cap, sizeof (char *));
	if (!seen.slot)
	{
		FREE (v);
		return ERR_OUT_OF_MEMORY;
	}

	uint made = 0;
	u64 total = 0;
	for (uint i = 0; i < b.count; i++)
	{
		const u8 *e = d + b.table + (size_t)i * BCBIG_ENTRY;
		const u32 no = bb_le32 (e), sz = bb_le32 (e + 4), off = bb_le32 (e + 8);
		if (off > size || sz > size - off)
			continue;
		total += sz;
		if (total > BCBIG_MAX_TOTAL)
			break;

		char path[PATH_MAX];
		size_t l = 0;
		if (no < size)
			for (; no + l < size && d[no + l] && l < 200; l++)
			{
				const unsigned char ch = d[no + l];
				path[l] = (ch < 0x20 || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"'
							  || ch == '<' || ch == '>' || ch == '|' || ch >= 0x7f)
					? '_'
					: (char)ch;
			}
		path[l] = 0;
		if (!path[0] || !strcmp (path, ".") || !strcmp (path, ".."))
			snprintf (path, sizeof (path), "entry%u", i);

		for (uint n = 2; !bcbig_claim (&seen, path) && n < 1000; n++)
		{
			// name.ext -> name_dupN.ext
			char *dot = strrchr (path, '.');
			char tail[256] = "";
			if (dot)
			{
				snprintf (tail, sizeof (tail), "%s", dot);
				*dot = 0;
			}
			char *us = strstr (path, "_dup");
			if (us && us[4] >= '0' && us[4] <= '9')
				*us = 0;
			l = strlen (path);
			snprintf (path + l, sizeof (path) - l, "_dup%u%s", n, tail);
		}

		u8 *out = MALLOC (sz ? sz : 1);
		if (!out)
			continue;
		memcpy (out, d + off, sz);
		v[made].name = STRDUP (path);
		v[made].data = out;
		v[made].size = sz;
		made++;
	}
	for (uint i = 0; i <= seen.mask; i++)
		FREE (seen.slot[i]);
	FREE (seen.slot);
	if (!made)
	{
		FREE (v);
		return ERR_NOTHING_TO_DO;
	}
	*entries = v;
	*n_entries = made;
	return ERR_OK;
}

// ".dspi": N DSP headers, then N channel bodies (see lib-bcbig.h).
static uint bcdspi_channels (const u8 *d, size_t size, u32 *nib_out)
{
	if (!d || size < 0x60)
		return 0;
	const u32 nsamp = (u32)d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3];
	const u32 nib = (u32)d[4] << 24 | d[5] << 16 | d[6] << 8 | d[7];
	const u32 rate = (u32)d[8] << 24 | d[9] << 16 | d[10] << 8 | d[11];
	if (!nsamp || !nib || rate < 4000 || rate > 96000 || nib / 2 > size)
		return 0;
	uint nch = 1;
	while (nch < 8 && (nch + 1) * 0x60 <= size && !memcmp (d + nch * 0x60, d, 12))
		nch++;
	const u64 per = ((u64)(nib + 1) / 2 + 7) & ~(u64)7;
	const u64 full = 0x60 * (u64)nch + per * nch;
	if (size > full || size + per < full)
		return 0;
	*nib_out = nib;
	return nch;
}

bool IsBcDspi (const u8 *data, size_t size)
{
	u32 nib;
	return bcdspi_channels (data, size, &nib) != 0;
}

enumError ScanBcDspi (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *d, size_t size)
{
	u32 nib;
	const uint nch = bcdspi_channels (d, size, &nib);
	if (!entries || !n_entries || !nch)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;
	const u64 bytes = ((u64)nib + 1) / 2, per = (bytes + 7) & ~(u64)7;
	nintendo_sarc_entry_t *v = CALLOC (nch, sizeof (*v));
	if (!v)
		return ERR_OUT_OF_MEMORY;
	uint made = 0;
	for (uint c = 0; c < nch; c++)
	{
		const u64 off = 0x60 * (u64)nch + per * c;
		if (off >= size)
			break;
		const u64 len = off + bytes <= size ? bytes : size - off;
		u8 *out = MALLOC (0x60 + len);
		if (!out)
			continue;
		memcpy (out, d + 0x60 * c, 0x60);
		memcpy (out + 0x60, d + off, len);
		char name[32];
		snprintf (name, sizeof (name), "ch%u.dsp", c);
		v[made].name = STRDUP (name);
		v[made].data = out;
		v[made].size = 0x60 + len;
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
