// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Artefacts Studio ".map" level databases; see lib-dbkmap.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-dbkmap.h"
#include "lib-diabolik.h"
#include "lib-image.h"
#include "lib-std.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define DM_BEGIN 0xbbbbbbbbu
#define DM_END 0xbebebebeu
#define DM_MAX_DEPTH 64
#define DM_MAX_TEXTURES 4096

bool IsDbkMap (const u8 *d, size_t size)
{
	return IsDiabolikRes (d, size, size);
}

// A valid section begins at 'p' inside the parent payload ending at 'limit'.
static bool dm_section (const u8 *d, size_t size, size_t p, size_t limit)
{
	if (p % 4 || p + 12 > limit || limit > size || rd_be32 (d + p) != DM_BEGIN)
		return false;
	const u32 end = rd_be32 (d + p + 4);
	return end % 4 == 0 && end >= p + 12 && end < limit && end + 4 <= size
		&& rd_be32 (d + end) == DM_END;
}

typedef struct dm_out_t
{
	nintendo_sarc_entry_t *v;
	uint used, alloc;
} dm_out_t;

typedef struct dm_ctx_t
{
	const u8 *d;
	size_t size;
	dm_out_t *out;
	uint textures;
} dm_ctx_t;

static bool dm_emit (dm_out_t *o, char *name, u8 *payload, uint size)
{
	if (!name || !payload)
	{
		FREE (name);
		FREE (payload);
		return false;
	}
	if (o->used == o->alloc)
	{
		const uint want = o->alloc ? o->alloc * 2 : 64;
		nintendo_sarc_entry_t *nv = REALLOC (o->v, want * sizeof (*nv));
		if (!nv)
		{
			FREE (name);
			FREE (payload);
			return false;
		}
		o->v = nv;
		o->alloc = want;
	}
	o->v[o->used].name = name;
	o->v[o->used].data = payload;
	o->v[o->used].size = size;
	o->used++;
	return true;
}

// Children of the section at 'p': calls cb for each; also reports the
// section's payload bounds. Returns the number of child sections.
typedef struct dm_node_t
{
	size_t start, end; // begin tag, closing tag offsets
} dm_node_t;

static uint dm_children (const dm_ctx_t *c, dm_node_t n, dm_node_t *kids, uint cap)
{
	uint count = 0;
	size_t p = n.start + 12;
	while (p + 12 <= n.end)
	{
		if (dm_section (c->d, c->size, p, n.end))
		{
			if (count < cap)
			{
				kids[count].start = p;
				kids[count].end = rd_be32 (c->d + p + 4);
			}
			count++;
			p = rd_be32 (c->d + p + 4) + 4;
		}
		else
			p += 4;
	}
	return count;
}

// First 8-byte raw record and first name in a subtree (document order).
typedef struct dm_hdr_t
{
	bool have_rec;
	u8 rec[8];
	char name[64];
} dm_hdr_t;

static void dm_scan_hdr (const dm_ctx_t *c, dm_node_t n, uint depth, dm_hdr_t *h)
{
	if (depth > DM_MAX_DEPTH)
		return;
	// raw gaps between children
	size_t p = n.start + 12, gs = p;
	while (p + 4 <= n.end)
	{
		if (p + 12 <= n.end && dm_section (c->d, c->size, p, n.end))
		{
			const dm_node_t k = { p, rd_be32 (c->d + p + 4) };
			if (gs < p)
			{
				// gap before this child
				const size_t len = p - gs;
				if (!h->have_rec && len == 8)
				{
					memcpy (h->rec, c->d + gs, 8);
					h->have_rec = true;
				}
			}
			dm_scan_hdr (c, k, depth + 1, h);
			p = k.end + 4;
			gs = p;
		}
		else
			p += 4;
	}
	if (gs < n.end)
	{
		const size_t len = n.end - gs;
		const u8 *g = c->d + gs;
		if (!h->have_rec && len == 8)
		{
			memcpy (h->rec, g, 8);
			h->have_rec = true;
		}
		if (!h->name[0] && len >= 8)
		{
			const u32 nl = rd_be32 (g);
			if (nl && nl <= 32 && 4 + nl <= len)
			{
				uint ok = 0;
				while (ok < nl && g[4 + ok] >= 0x20 && g[4 + ok] < 0x7f)
					ok++;
				if (ok && (ok == nl || g[4 + ok] == 0))
				{
					memcpy (h->name, g + 4, ok);
					h->name[ok] = 0;
				}
			}
		}
	}
}

static char *dm_member_name (uint index, ccp name, ccp ext)
{
	char clean[80];
	uint n = 0;
	for (ccp p = name; p && *p && n + 1 < sizeof (clean); p++)
	{
		const u8 ch = (u8)*p;
		clean[n++] = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
				|| ch == '.' || ch == '-' || ch == '_'
			? (char)ch
			: '_';
	}
	clean[n] = 0;
	if (!n)
		snprintf (clean, sizeof (clean), "unnamed");
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "textures/%04u_%s%s", index, clean, ext);
	return STRDUP (path);
}

// Single-image TPL wrapping one raw GX mip level (same construction as
// lib-goliath.c / lib-ptlg.c).
static u8 *dm_make_tpl (u32 w, u32 h, u32 iform, const u8 *pixels, u32 pixel_size, uint *out_size)
{
	const u32 tpl_hdr = sizeof (tpl_header_t);
	const u32 tpl_tab = tpl_hdr + sizeof (tpl_imgtab_t);
	const u32 tpl_data = tpl_tab + sizeof (tpl_img_header_t);
	u8 *tpl = CALLOC (tpl_data + pixel_size, 1);
	if (!tpl)
		return 0;
	write_be32 (tpl, TPL_MAGIC_NUM);
	write_be32 (tpl + 4, 1);
	write_be32 (tpl + 8, tpl_hdr);
	write_be32 (tpl + tpl_hdr, tpl_tab);
	write_be32 (tpl + tpl_hdr + 4, 0);
	write_be16 (tpl + tpl_tab, (u16)h);
	write_be16 (tpl + tpl_tab + 2, (u16)w);
	write_be32 (tpl + tpl_tab + 4, iform);
	write_be32 (tpl + tpl_tab + 8, tpl_data);
	write_be32 (tpl + tpl_tab + 20, 1);
	write_be32 (tpl + tpl_tab + 24, 1);
	memcpy (tpl + tpl_data, pixels, pixel_size);
	*out_size = tpl_data + pixel_size;
	return tpl;
}

// CMPR level byte size: 8x8 pixel tiles of 32 bytes, at least one tile.
static u64 dm_cmpr_level (u32 w, u32 h)
{
	return (u64)((w + 7) / 8) * ((h + 7) / 8) * 32;
}

static void dm_texture (dm_ctx_t *c, dm_node_t obj, dm_node_t leaf)
{
	const u8 *g = c->d + leaf.start + 12;
	const size_t len = leaf.end - leaf.start - 12;
	if (len < 13 || g[0] != 1 || rd_be32 (g + 9))
		return;
	const u32 mips = rd_be32 (g + 1), sz = rd_be32 (g + 5);
	if (!mips || mips > 16 || sz > len - 13 || len - 13 - sz >= 8)
		return;

	dm_hdr_t h;
	memset (&h, 0, sizeof (h));
	dm_scan_hdr (c, obj, 0, &h);
	if (!h.have_rec || h.rec[2] != 1)
		return;
	const u32 w = h.rec[3] << 8 | h.rec[4], ht = h.rec[5] << 8 | h.rec[6];
	if (!w || !ht || w > 0x2000 || ht > 0x2000)
		return;

	// verify the chain adds up: largest level first, each level halved
	u64 want = 0;
	u32 lw = w, lh = ht;
	for (u32 i = 0; i < mips; i++)
	{
		want += dm_cmpr_level (lw, lh);
		lw = lw > 1 ? lw / 2 : 1;
		lh = lh > 1 ? lh / 2 : 1;
	}
	if (want != sz)
		return;
	const u32 base = (u32)dm_cmpr_level (w, ht);
	uint tpl_size = 0;
	u8 *tpl = dm_make_tpl (w, ht, IMG_CMPR, g + 13, base, &tpl_size);
	if (dm_emit (c->out, dm_member_name (c->textures, h.name, ".tpl"), tpl, tpl_size))
		c->textures++;
}

static void dm_walk (dm_ctx_t *c, dm_node_t n, uint depth)
{
	if (depth > DM_MAX_DEPTH || c->textures >= DM_MAX_TEXTURES)
		return;
	dm_node_t kids[32];
	const uint nk = dm_children (c, n, kids, 32);
	if (nk > 32)
	{
		// too many children for the stack buffer: walk them in a second pass
		size_t p = n.start + 12;
		while (p + 12 <= n.end)
		{
			if (dm_section (c->d, c->size, p, n.end))
			{
				const dm_node_t k = { p, rd_be32 (c->d + p + 4) };
				dm_walk (c, k, depth + 1);
				p = k.end + 4;
			}
			else
				p += 4;
		}
		return;
	}
	// texture object: its last child section is the pixel leaf, which itself
	// has no nested sections
	if (nk >= 2)
	{
		const dm_node_t last = kids[nk - 1];
		dm_node_t sub[1];
		if (!dm_children (c, last, sub, 1))
		{
			const size_t before = c->out->used;
			dm_texture (c, n, last);
			if (c->out->used != before)
				return;
		}
	}
	for (uint i = 0; i < nk; i++)
		dm_walk (c, kids[i], depth + 1);
}

enumError ScanDbkMap (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *d, size_t size)
{
	if (!entries || !n_entries || !IsDbkMap (d, size))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	dm_out_t out = { 0, 0, 0 };
	dm_ctx_t c = { d, size, &out, 0 };
	if (dm_section (d, size, 8, size - 4))
	{
		const dm_node_t root = { 8, rd_be32 (d + 12) };
		dm_walk (&c, root, 0);
	}
	if (!out.used)
	{
		FREE (out.v);
		return ERR_NOTHING_TO_DO;
	}
	*entries = out.v;
	*n_entries = out.used;
	return ERR_OK;
}
