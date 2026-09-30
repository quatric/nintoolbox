// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Exient "XPK" archives; see lib-xpk.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-xpk.h"
#include "lib-std.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <zlib.h>

#define XPK_MAGIC 0x58504b01u
#define XPK_TABLE 0x50
#define XPK_ENTRY 32
#define XPK_MAX_ENTRIES 0x40000
#define XPK_MAX_NAMES 0x1000000u
#define XPK_MAX_TOTAL 0x60000000ull

static u32 xp_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

typedef struct xpk_t
{
	uint count;
	u32 names_size;
	u64 names_off;
} xpk_t;

// Validates the table geometry and derives the entry count.
static bool xpk_geometry (xpk_t *x, const u8 *d, size_t size)
{
	if (!d || size < XPK_TABLE + XPK_ENTRY || xp_be32 (d) != XPK_MAGIC)
		return false;
	const u32 files = xp_be32 (d + 8), nsize = xp_be32 (d + 12);
	if (!files || files > XPK_MAX_ENTRIES || !nsize || nsize > XPK_MAX_NAMES)
		return false;

	// smallest file data offset among the rows we can see
	const u64 max_rows = (u64)files + 4096 < (size - XPK_TABLE) / XPK_ENTRY ? (u64)files + 4096
		: (size - XPK_TABLE) / XPK_ENTRY;
	u64 min_off = ~(u64)0;
	for (u64 i = 0; i < max_rows; i++)
	{
		const u8 *e = d + XPK_TABLE + i * XPK_ENTRY;
		const u32 sz = xp_be32 (e + 8), off = xp_be32 (e + 12);
		if (sz && off >= XPK_TABLE + XPK_ENTRY && off < min_off)
			min_off = off;
	}
	if (min_off == ~(u64)0 || min_off < XPK_TABLE + nsize)
		return false;
	const u64 span = min_off - XPK_TABLE - nsize;
	if (span % XPK_ENTRY || span / XPK_ENTRY < files || span / XPK_ENTRY > files + 4096u)
		return false;
	x->count = (uint)(span / XPK_ENTRY);
	x->names_size = nsize;
	x->names_off = XPK_TABLE + span;
	return x->names_off + nsize <= size;
}

bool IsXpk (const u8 *data, size_t size)
{
	xpk_t x;
	return xpk_geometry (&x, data, size);
}

// Path of entry i, walking parents; parent[] is filled from the dir ranges.
static void xpk_path (char *out, size_t cap, const u8 *d, const xpk_t *x, const int *parent, uint i)
{
	int chain[32], n = 0;
	for (int c = (int)i; c >= 0 && n < 32; c = parent[c])
		chain[n++] = c;
	out[0] = 0;
	size_t used = 0;
	for (int k = n - 1; k >= 0; k--)
	{
		const u8 *e = d + XPK_TABLE + (size_t)chain[k] * XPK_ENTRY;
		const u32 no = xp_be32 (e + 4);
		char part[256];
		if (no < x->names_size)
		{
			const char *s = (const char *)d + x->names_off + no;
			const size_t avail = x->names_size - no;
			size_t l = 0;
			while (l < avail && s[l] && l < sizeof (part) - 1)
			{
				const unsigned char ch = (unsigned char)s[l];
				part[l] = (ch < 0x20 || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"'
							  || ch == '<' || ch == '>' || ch == '|' || ch >= 0x7f)
					? '_'
					: (char)ch;
				l++;
			}
			part[l] = 0;
		}
		else
			part[0] = 0;
		if (!part[0] || !strcmp (part, ".") || !strcmp (part, ".."))
			snprintf (part, sizeof (part), "entry%u", (uint)chain[k]);
		used += snprintf (out + used, cap - used, "%s%s", k == n - 1 ? "" : "/", part);
		if (used >= cap)
			break;
	}
}

// Case-insensitive path set: the archive can hold names that differ only in
// case (or repeat outright), and the host file system may fold case, so
// later duplicates get a "_dupN" suffix instead of aborting the extraction.
typedef struct xpk_seen_t
{
	char **slot;
	uint mask;
} xpk_seen_t;

static uint xpk_hash (ccp s)
{
	uint h = 2166136261u;
	for (; *s; s++)
		h = (h ^ (u8)((*s >= 'A' && *s <= 'Z') ? *s + 32 : *s)) * 16777619u;
	return h;
}

// Returns true if 'path' was new (and records it); false if already present.
static bool xpk_claim (xpk_seen_t *t, ccp path)
{
	uint i = xpk_hash (path) & t->mask;
	while (t->slot[i])
	{
		if (!strcasecmp (t->slot[i], path))
			return false;
		i = (i + 1) & t->mask;
	}
	t->slot[i] = STRDUP (path);
	return true;
}

enumError ScanXpk (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *d, size_t size)
{
	xpk_t x;
	if (!entries || !n_entries || !xpk_geometry (&x, d, size))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	// parent map from directory ranges
	int *parent = MALLOC (sizeof (int) * x.count);
	if (!parent)
		return ERR_OUT_OF_MEMORY;
	for (uint i = 0; i < x.count; i++)
		parent[i] = -1;
	for (uint i = 0; i < x.count; i++)
	{
		const u8 *e = d + XPK_TABLE + (size_t)i * XPK_ENTRY;
		if (xp_be32 (e + 8))
			continue;
		const u32 first = xp_be32 (e + 12), n = xp_be32 (e + 24);
		for (u32 k = 0; k < n && (u64)first + k < x.count; k++)
			if (first + k != i)
				parent[first + k] = (int)i;
	}

	nintendo_sarc_entry_t *v = CALLOC (x.count, sizeof (*v));
	if (!v)
	{
		FREE (parent);
		return ERR_OUT_OF_MEMORY;
	}

	xpk_seen_t seen;
	uint cap = 16;
	while (cap < x.count * 2)
		cap <<= 1;
	seen.mask = cap - 1;
	seen.slot = CALLOC (cap, sizeof (char *));
	if (!seen.slot)
	{
		FREE (parent);
		FREE (v);
		return ERR_OUT_OF_MEMORY;
	}

	uint made = 0;
	u64 total = 0;
	for (uint i = 0; i < x.count; i++)
	{
		const u8 *e = d + XPK_TABLE + (size_t)i * XPK_ENTRY;
		const u32 usize = xp_be32 (e + 8), off = xp_be32 (e + 12), flags = xp_be32 (e + 16),
				  csize = xp_be32 (e + 24);
		if (!usize) // directory
			continue;
		total += usize;
		if (total > XPK_MAX_TOTAL)
			break;

		u8 *out = 0;
		if (flags == 1)
		{
			if (off > size || csize > size - off)
				continue;
			out = MALLOC (usize);
			if (!out)
				continue;
			uLongf dl = usize;
			if (uncompress (out, &dl, d + off, csize) != Z_OK || dl != usize)
			{
				FREE (out);
				continue;
			}
		}
		else
		{
			if (off > size || usize > size - off)
				continue;
			out = MALLOC (usize);
			if (!out)
				continue;
			memcpy (out, d + off, usize);
		}

		char path[PATH_MAX];
		xpk_path (path, sizeof (path), d, &x, parent, i);
		for (uint n = 2; !xpk_claim (&seen, path) && n < 1000; n++)
		{
			// name.ext -> name_dupN.ext
			xpk_path (path, sizeof (path) - 16, d, &x, parent, i);
			char *dot = strrchr (path, '.'), *slash = strrchr (path, '/');
			char tail[PATH_MAX] = "";
			if (dot && (!slash || dot > slash))
			{
				snprintf (tail, sizeof (tail), "%s", dot);
				*dot = 0;
			}
			size_t l = strlen (path);
			snprintf (path + l, 16, "_dup%u", n);
			l = strlen (path);
			snprintf (path + l, sizeof (path) - l, "%s", tail);
		}
		v[made].name = STRDUP (path);
		v[made].data = out;
		v[made].size = usize;
		made++;
	}
	FREE (parent);
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
