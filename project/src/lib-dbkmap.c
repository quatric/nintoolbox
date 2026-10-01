// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Artefacts Studio ".map" level databases; see lib-dbkmap.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-dbkmap.h"
#include "lib-diabolik.h"
#include "lib-image.h"
#include "lib-model-glb.h"
#include "lib-std.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

typedef struct dm_texinfo_t
{
	size_t obj_start; // start of the texture object section
	char name[96]; // member name, e.g. "textures/0003_sky_jungle4.tpl"
} dm_texinfo_t;

typedef struct dm_ctx_t
{
	const u8 *d;
	size_t size;
	dm_out_t *out; // NULL: only record names
	uint textures;
	dm_texinfo_t *tex;
	uint tex_cap;
	const u8 *want; // NULL: emit every texture, else one flag per texture index
	ccp prefix; // member name prefix after "textures/"
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

static char *dm_member_name (ccp prefix, uint index, ccp name, ccp ext)
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
	snprintf (path, sizeof (path), "textures/%s%04u_%s%s", prefix ? prefix : "", index, clean, ext);
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
	char *mname = dm_member_name (c->prefix, c->textures, h.name, ".tpl");
	if (!mname)
		return;
	if (c->out && (!c->want || c->want[c->textures]))
	{
		uint tpl_size = 0;
		u8 *tpl = dm_make_tpl (w, ht, IMG_CMPR, g + 13, base, &tpl_size);
		if (!tpl || !dm_emit (c->out, STRDUP (mname), tpl, tpl_size))
		{
			FREE (mname);
			return;
		}
	}
	if (c->textures == c->tex_cap)
	{
		const uint want = c->tex_cap ? c->tex_cap * 2 : 32;
		dm_texinfo_t *nt = REALLOC (c->tex, want * sizeof (*nt));
		if (nt)
		{
			c->tex = nt;
			c->tex_cap = want;
		}
	}
	if (c->textures < c->tex_cap)
	{
		c->tex[c->textures].obj_start = obj.start;
		snprintf (c->tex[c->textures].name, sizeof (c->tex[c->textures].name), "%s", mname);
	}
	c->textures++;
	FREE (mname);
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
			const uint before = c->textures;
			dm_texture (c, n, last);
			if (c->textures != before)
				return;
		}
	}
	for (uint i = 0; i < nk; i++)
		dm_walk (c, kids[i], depth + 1);
}

//-----------------------------------------------------------------------------
// object references: material set -> material -> texture

typedef struct dm_ref_t
{
	int cls;
	u32 ua, ub;
	uint n;
	u8 flag;
	size_t end; // offset after the flag byte
} dm_ref_t;

// An object reference as the game's reader FUN_8000f2d8 stores it in a
// FAAFFAAF stream: s16 class, s16 n, u64 uid, [u32 size when n > 0], u8 flag
// (1 = the object's body follows as the next section). No name (it is only
// present in the byte-swapped twin format).
static bool dm_ref_at (const u8 *d, size_t p, size_t limit, dm_ref_t *r)
{
	if (p + 13 > limit)
		return false;
	const int cls = (s16)(d[p] << 8 | d[p + 1]);
	const int n = (s16)(d[p + 2] << 8 | d[p + 3]);
	if (cls <= 0 || cls > 0x300 || n < 0 || n > 1)
		return false;
	size_t q = p + 12;
	if (n)
		q += 4;
	if (q + 1 > limit || d[q] > 1)
		return false;
	r->cls = cls;
	r->n = n;
	r->ua = rd_be32 (d + p + 4);
	r->ub = rd_be32 (d + p + 8);
	r->flag = d[q];
	r->end = q + 1;
	return r->ua || r->ub;
}

typedef struct dm_obj_t
{
	int cls;
	u32 ua, ub;
	size_t start, end; // body section
} dm_obj_t;

typedef struct dm_index_t
{
	dm_obj_t *v;
	uint used, cap;
} dm_index_t;

static void dm_index_add (dm_index_t *ix, const dm_ref_t *r, dm_node_t body)
{
	if (ix->used == ix->cap)
	{
		const uint want = ix->cap ? ix->cap * 2 : 256;
		dm_obj_t *nv = REALLOC (ix->v, want * sizeof (*nv));
		if (!nv)
			return;
		ix->v = nv;
		ix->cap = want;
	}
	ix->v[ix->used++] = (dm_obj_t){ r->cls, r->ua, r->ub, body.start, body.end };
}

// Every section that directly follows an object reference with flag 1.
static void dm_index_walk (const dm_ctx_t *c, dm_index_t *ix, dm_node_t n, uint depth)
{
	if (depth > DM_MAX_DEPTH)
		return;
	size_t p = n.start + 12, gs = p;
	while (p + 12 <= n.end)
	{
		if (dm_section (c->d, c->size, p, n.end))
		{
			const dm_node_t k = { p, rd_be32 (c->d + p + 4) };
			// the reference that introduces this body ends at the section
			// (plus up to 3 padding bytes); references pack without padding
			for (uint back = 13; back <= 23; back++)
			{
				dm_ref_t r;
				if (p >= gs + back && dm_ref_at (c->d, p - back, p, &r) && r.flag
					&& ((r.end + 3) & ~(size_t)3) == p)
				{
					dm_index_add (ix, &r, k);
					break;
				}
			}
			dm_index_walk (c, ix, k, depth + 1);
			p = k.end + 4;
			gs = p;
		}
		else
			p += 4;
	}
}

static const dm_obj_t *dm_index_find (const dm_index_t *ix, int cls, u32 ua, u32 ub)
{
	for (uint i = 0; i < ix->used; i++)
		if (ix->v[i].ua == ua && ix->v[i].ub == ub && (cls < 0 || ix->v[i].cls == cls))
			return ix->v + i;
	return 0;
}

// First object reference of class CLS in the gaps of a subtree (document order).
static bool dm_first_ref (const dm_ctx_t *c, dm_node_t n, int cls, uint depth, dm_ref_t *out)
{
	if (depth > DM_MAX_DEPTH)
		return false;
	size_t p = n.start + 12, gs = p;
	while (p + 4 <= n.end)
	{
		if (p + 12 <= n.end && dm_section (c->d, c->size, p, n.end))
		{
			for (size_t q = gs; q + 13 <= p; q++)
				if (dm_ref_at (c->d, q, p, out) && out->cls == cls)
					return true;
			const dm_node_t k = { p, rd_be32 (c->d + p + 4) };
			if (dm_first_ref (c, k, cls, depth + 1, out))
				return true;
			p = k.end + 4;
			gs = p;
		}
		else
			p += 4;
	}
	for (size_t q = gs; q + 13 <= n.end; q++)
		if (dm_ref_at (c->d, q, n.end, out) && out->cls == cls)
			return true;
	return false;
}

// Ordered object references of a material set body: descend through
// single-child wrappers to the section that holds the list, skip its u32 count
// and parse the references of every gap in order.
#define DM_MAX_SET 256
static uint dm_material_set (const dm_ctx_t *c, dm_node_t body, dm_ref_t *refs)
{
	dm_node_t n = body;
	for (uint guard = 0; guard < 8; guard++)
	{
		dm_node_t kids[2];
		const uint nk = dm_children (c, n, kids, 2);
		if (nk != 1)
			break;
		n = kids[0];
	}
	uint count = 0;
	bool first = true;
	size_t p = n.start + 12, gs = p;
	while (p <= n.end)
	{
		const bool at_sec = p + 12 <= n.end && dm_section (c->d, c->size, p, n.end);
		if (at_sec || p + 4 > n.end)
		{
			const size_t ge = at_sec ? p : n.end;
			size_t q = gs;
			if (ge > gs && first)
			{
				// the list count comes first and is followed by the first reference
				if (ge - gs >= 4 + 13 && rd_be32 (c->d + gs) <= DM_MAX_SET)
					q += 4;
				first = false;
			}
			dm_ref_t r;
			while (q < ge && count < DM_MAX_SET && dm_ref_at (c->d, q, ge, &r))
			{
				refs[count++] = r;
				q = r.end;
			}
			if (!at_sec)
				break;
			const dm_node_t k = { p, rd_be32 (c->d + p + 4) };
			p = k.end + 4;
			gs = p;
		}
		else
			p += 4;
	}
	return count;
}

//-----------------------------------------------------------------------------
// meshes

#define DM_SLOTS 32
#define DM_MAX_VERTS 0x40000u

typedef struct dm_seg_t
{
	uint slot, first, count, tris;
	const u8 *dl;
	uint dl_size;
} dm_seg_t;

typedef struct dm_mesh_t
{
	uint V, T;
	const u8 *verts, *uv;
	dm_seg_t *seg;
	uint n_seg;
} dm_mesh_t;

typedef struct dm_rd_t
{
	const u8 *p;
	size_t len, pos;
	bool bad;
} dm_rd_t;

static const u8 *dm_take (dm_rd_t *r, size_t n)
{
	if (r->bad || n > r->len - r->pos)
	{
		r->bad = true;
		return 0;
	}
	const u8 *q = r->p + r->pos;
	r->pos += n;
	return q;
}

static u32 dm_u32 (dm_rd_t *r)
{
	const u8 *q = dm_take (r, 4);
	return q ? rd_be32 (q) : 0;
}

static uint dm_u16 (dm_rd_t *r)
{
	const u8 *q = dm_take (r, 2);
	return q ? q[0] << 8 | q[1] : 0;
}

// Parse a mesh leaf payload (see lib-dbkmap.h). The layout must consume the
// payload to within its alignment padding, which every non-mesh leaf fails.
static bool dm_mesh_parse (dm_mesh_t *m, const u8 *g, size_t len)
{
	memset (m, 0, sizeof (*m));
	dm_rd_t r = { g, len, 0, false };
	const u8 *flag = dm_take (&r, 1);
	if (!flag || *flag != 1)
		return false;
	m->V = dm_u32 (&r);
	if (!m->V || m->V > DM_MAX_VERTS)
		return false;
	m->verts = dm_take (&r, (size_t)m->V * 16);
	m->T = dm_u32 (&r);
	if (r.bad || m->T > DM_MAX_VERTS * 4)
		return false;
	m->uv = dm_take (&r, (size_t)m->T * 4);
	const u32 P = dm_u32 (&r);
	if (r.bad || P > DM_MAX_VERTS * 8)
		return false;
	dm_take (&r, (size_t)P * 6);
	const u32 C = dm_u32 (&r);
	if (r.bad || C > DM_MAX_VERTS * 8)
		return false;
	dm_take (&r, (size_t)C * 4);
	if (r.bad)
		return false;

	uint cap = 0;
	for (uint i = 0; i < DM_SLOTS && !r.bad; i++)
	{
		for (uint kind = 0; kind < 3; kind++)
		{
			const u8 *np = dm_take (&r, 1);
			if (!np)
				break;
			for (uint e = 0; e < *np && !r.bad; e++)
			{
				if (kind == 2)
				{
					const u32 k = dm_u32 (&r);
					dm_u32 (&r);
					dm_take (&r, (size_t)k * 8);
					continue;
				}
				const uint first = dm_u16 (&r), count = dm_u16 (&r);
				const u32 tris = dm_u32 (&r);
				dm_u16 (&r);
				const u32 dsz = dm_u32 (&r);
				const u8 *dl = dm_take (&r, dsz);
				if (kind == 0)
					dm_take (&r, 8);
				if (r.bad)
					break;
				if (kind != 0)
					continue;
				if (m->n_seg == cap)
				{
					cap = cap ? cap * 2 : 16;
					dm_seg_t *ns = REALLOC (m->seg, cap * sizeof (*ns));
					if (!ns)
					{
						r.bad = true;
						break;
					}
					m->seg = ns;
				}
				dm_seg_t *sg = m->seg + m->n_seg++;
				sg->slot = i;
				sg->first = first;
				sg->count = count;
				sg->tris = tris;
				sg->dl = dl;
				sg->dl_size = dsz;
			}
		}
	}
	if (r.bad || r.pos > len || len - r.pos >= 8 || !m->n_seg)
	{
		FREE (m->seg);
		m->seg = 0;
		return false;
	}
	return true;
}

typedef struct dm_tri_t
{
	uint v[3], t[3], slot;
} dm_tri_t;

// Expand one display list (0x98 strips, 0x90 lists, 0xA0 fans; vertices are
// three u16: position, normal, texcoord) into triangles.
static bool dm_expand_dl (const dm_mesh_t *m, const dm_seg_t *sg, dm_tri_t **tri, uint *n, uint *cap)
{
	const u8 *d = sg->dl;
	size_t i = 0;
	while (i < sg->dl_size)
	{
		const u8 op = d[i++];
		if (!op)
			continue;
		const u8 prim = op & 0xf8;
		if (prim != 0x90 && prim != 0x98 && prim != 0xa0)
			return false;
		if (i + 2 > sg->dl_size)
			return false;
		const uint cnt = d[i] << 8 | d[i + 1];
		i += 2;
		if (i + (size_t)cnt * 6 > sg->dl_size)
			return false;
		uint pv[3], pt[3];
		uint have = 0;
		for (uint k = 0; k < cnt; k++, i += 6)
		{
			const uint p = d[i] << 8 | d[i + 1], t = d[i + 4] << 8 | d[i + 5];
			if (p >= sg->count || sg->first + p >= m->V || t >= m->T)
				return false;
			uint a[3], at[3];
			bool emit = false;
			if (prim == 0x90)
			{
				pv[have] = sg->first + p;
				pt[have++] = t;
				if (have == 3)
				{
					memcpy (a, pv, sizeof (a));
					memcpy (at, pt, sizeof (at));
					have = 0;
					emit = true;
				}
			}
			else if (prim == 0x98)
			{
				if (have < 3)
				{
					pv[have] = sg->first + p;
					pt[have++] = t;
				}
				else
				{
					pv[0] = pv[1];
					pv[1] = pv[2];
					pv[2] = sg->first + p;
					pt[0] = pt[1];
					pt[1] = pt[2];
					pt[2] = t;
				}
				if (have == 3)
				{
					const bool odd = (k - 2) & 1;
					a[0] = pv[0];
					a[1] = odd ? pv[2] : pv[1];
					a[2] = odd ? pv[1] : pv[2];
					at[0] = pt[0];
					at[1] = odd ? pt[2] : pt[1];
					at[2] = odd ? pt[1] : pt[2];
					emit = true;
				}
			}
			else // fan
			{
				if (have < 3)
				{
					pv[have] = sg->first + p;
					pt[have++] = t;
				}
				else
				{
					pv[1] = pv[2];
					pt[1] = pt[2];
					pv[2] = sg->first + p;
					pt[2] = t;
				}
				if (have == 3)
				{
					memcpy (a, pv, sizeof (a));
					memcpy (at, pt, sizeof (at));
					emit = true;
				}
			}
			if (!emit || a[0] == a[1] || a[1] == a[2] || a[0] == a[2])
				continue;
			if (*n == *cap)
			{
				*cap = *cap ? *cap * 2 : 1024;
				dm_tri_t *nt = REALLOC (*tri, *cap * sizeof (**tri));
				if (!nt)
					return false;
				*tri = nt;
			}
			dm_tri_t *t3 = *tri + (*n)++;
			memcpy (t3->v, a, sizeof (a));
			memcpy (t3->t, at, sizeof (at));
			t3->slot = sg->slot;
		}
	}
	return true;
}

static inline float dm_s16 (const u8 *p)
{
	return (float)(s16)(p[0] << 8 | p[1]);
}

static inline float dm_s8 (u8 b)
{
	return (float)(int8_t)b;
}

// Skeleton of a mesh object (class 0x1c, loader FUN_800bf004): after the base
// section come five u16, then three counted arrays; the first holds the bones
// {s32 parent, f32 pos[3], f32 rot[9], name28} (80 bytes, parents first), the
// second the inverse bind transforms of the bones that skin vertices.
typedef struct dm_bone_t
{
	int parent;
	double pos[3];
	double rot[9]; // as stored: row-major, the local rotation is its transpose
	char name[24];
} dm_bone_t;

typedef struct dm_skel_t
{
	dm_bone_t *b;
	uint n;
} dm_skel_t;

static double dm_f32 (const u8 *p)
{
	union
	{
		u32 u;
		float f;
	} x = { rd_be32 (p) };
	return x.f;
}

// BODY is the mesh object section; its first child is the base section.
static bool dm_skel_parse (const dm_ctx_t *c, dm_node_t body, dm_skel_t *sk)
{
	memset (sk, 0, sizeof (*sk));
	dm_node_t kids[1];
	if (!dm_children (c, body, kids, 1))
		return false;
	size_t p = kids[0].end + 4 + 10; // five u16
	if (p + 4 > body.end)
		return false;
	const u32 n = rd_be32 (c->d + p);
	p += 4;
	if (!n || n > 512 || p + (size_t)n * 80 > body.end)
		return false;
	sk->b = CALLOC (n, sizeof (*sk->b));
	if (!sk->b)
		return false;
	for (uint i = 0; i < n; i++, p += 80)
	{
		dm_bone_t *b = sk->b + i;
		b->parent = (int)rd_be32 (c->d + p);
		if (b->parent >= (int)i || b->parent < -1)
		{
			FREE (sk->b);
			sk->b = 0;
			return false;
		}
		for (uint k = 0; k < 3; k++)
			b->pos[k] = dm_f32 (c->d + p + 4 + 4 * k);
		for (uint k = 0; k < 9; k++)
			b->rot[k] = dm_f32 (c->d + p + 16 + 4 * k);
		memcpy (b->name, c->d + p + 56, 20);
		b->name[20] = 0;
	}
	sk->n = n;
	return true;
}

// Euler angles (degrees) of M = Rz(z) Ry(y) Rx(x), the order lib-model-glb
// composes joint rotations in.
static void dm_euler_zyx (const double m[9], vec3_t *out)
{
	const double sy = -m[6];
	double x, y, z;
	if (sy > 0.99999 || sy < -0.99999)
	{
		y = sy > 0 ? M_PI / 2 : -M_PI / 2;
		x = atan2 (-m[5], m[4]);
		z = 0;
	}
	else
	{
		y = asin (sy);
		x = atan2 (m[7], m[8]);
		z = atan2 (m[3], m[0]);
	}
	out->x = (float)(x * 180.0 / M_PI);
	out->y = (float)(y * 180.0 / M_PI);
	out->z = (float)(z * 180.0 / M_PI);
}

// Fill model->joints from the skeleton, converted from the game's Z-up to
// glTF's Y-up with C = [[1,0,0],[0,0,1],[0,-1,0]] ((x,y,z) -> (x,z,-y)).
static bool dm_build_joints (model_t *model, const dm_skel_t *sk)
{
	static const double C[9] = { 1, 0, 0, 0, 0, 1, 0, -1, 0 };
	model->joints = CALLOC (sk->n, sizeof (joint_t));
	if (!model->joints)
		return false;
	double (*wr)[9] = CALLOC (sk->n, sizeof (*wr)); // world rotation, Z-up
	double (*wt)[3] = CALLOC (sk->n, sizeof (*wt));
	if (!wr || !wt)
	{
		FREE (wr);
		FREE (wt);
		FREE (model->joints);
		model->joints = 0;
		return false;
	}
	for (uint i = 0; i < sk->n; i++)
	{
		const dm_bone_t *b = sk->b + i;
		double L[9]; // local rotation = transpose of the stored matrix
		for (uint r = 0; r < 3; r++)
			for (uint k = 0; k < 3; k++)
				L[r * 3 + k] = b->rot[k * 3 + r];
		if (b->parent < 0)
		{
			memcpy (wr[i], L, sizeof (L));
			memcpy (wt[i], b->pos, sizeof (b->pos));
		}
		else
		{
			const double *pr = wr[b->parent];
			for (uint r = 0; r < 3; r++)
			{
				wt[i][r] = wt[b->parent][r]
					+ pr[r * 3] * b->pos[0] + pr[r * 3 + 1] * b->pos[1] + pr[r * 3 + 2] * b->pos[2];
				for (uint k = 0; k < 3; k++)
					wr[i][r * 3 + k] = pr[r * 3] * L[k] + pr[r * 3 + 1] * L[3 + k] + pr[r * 3 + 2] * L[6 + k];
			}
		}

		joint_t *j = model->joints + i;
		snprintf (j->name, sizeof (j->name), "%s", b->name[0] ? b->name : "bone");
		j->parent_idx = b->parent;
		// local transform in Y-up: R' = C L C^T, t' = C t
		double CL[9], R2[9];
		for (uint r = 0; r < 3; r++)
			for (uint k = 0; k < 3; k++)
				CL[r * 3 + k] = C[r * 3] * L[k] + C[r * 3 + 1] * L[3 + k] + C[r * 3 + 2] * L[6 + k];
		for (uint r = 0; r < 3; r++)
			for (uint k = 0; k < 3; k++)
				R2[r * 3 + k] = CL[r * 3] * C[k * 3] + CL[r * 3 + 1] * C[k * 3 + 1] + CL[r * 3 + 2] * C[k * 3 + 2];
		dm_euler_zyx (R2, &j->rotate);
		j->translate = (vec3_t){ (float)b->pos[0], (float)b->pos[2], (float)-b->pos[1] };
		j->scale = (vec3_t){ 1, 1, 1 };

		// world bind (Y-up) and its inverse, 3x4 row-major
		double WR[9], WT[3];
		for (uint r = 0; r < 3; r++)
		{
			WT[r] = C[r * 3] * wt[i][0] + C[r * 3 + 1] * wt[i][1] + C[r * 3 + 2] * wt[i][2];
			for (uint k = 0; k < 3; k++)
			{
				double a = 0;
				for (uint e = 0; e < 3; e++)
				{
					double ce = 0;
					for (uint f = 0; f < 3; f++)
						ce += wr[i][e * 3 + f] * C[k * 3 + f];
					a += C[r * 3 + e] * ce;
				}
				WR[r * 3 + k] = a;
			}
		}
		for (uint r = 0; r < 3; r++)
		{
			for (uint k = 0; k < 3; k++)
			{
				j->bind[r * 4 + k] = (float)WR[r * 3 + k];
				j->inverse_bind[r * 4 + k] = (float)WR[k * 3 + r]; // transpose
			}
			j->bind[r * 4 + 3] = (float)WT[r];
		}
		for (uint r = 0; r < 3; r++)
			j->inverse_bind[r * 4 + 3]
				= (float)-(WR[0 * 3 + r] * WT[0] + WR[1 * 3 + r] * WT[1] + WR[2 * 3 + r] * WT[2]);
		j->has_inverse_bind = 1;
	}
	FREE (wr);
	FREE (wt);
	model->num_joints = sk->n;
	return true;
}

//-----------------------------------------------------------------------------
// animations (class 0x2e, loader FUN_800b6e30): a header, one flag byte per
// bone, then pooled quantised keys

typedef struct dm_anim_t
{
	char name[64];
	uint frames, bones, ms_per_frame;
	const u8 *flags; // per bone: 1 rotation constant, 2 translation constant,
	// 0x20 no rotation, 0x40 no translation
	const u8 *rot; // s16 x4 / 16384 quaternions
	const u8 *tr; // s16 x3 * scale translations
	uint n_rot, n_tr;
	float tr_scale;
	const u8 *idx_tr; // u16 per frame per (non-constant) bone
	uint n_idx_tr;
	const u8 *idx_rot; // same for rotations; absent when every frame has its own key
	uint n_idx_rot;
} dm_anim_t;

static bool dm_anim_parse (const u8 *g, size_t len, dm_anim_t *a)
{
	memset (a, 0, sizeof (*a));
	dm_rd_t r = { g, len, 0, false };
	a->ms_per_frame = dm_u32 (&r);
	dm_u32 (&r); // duration
	a->frames = dm_u16 (&r);
	a->bones = dm_u16 (&r);
	dm_take (&r, 4); // flags
	a->n_rot = dm_u32 (&r);
	a->n_tr = dm_u32 (&r);
	const u32 n_sc = dm_u32 (&r);
	const u8 *p4 = dm_take (&r, 4);
	a->tr_scale = p4 ? (float)dm_f32 (p4) : 0.0f;
	dm_take (&r, 4); // scale scale
	if (r.bad || !a->frames || a->frames > 20000 || !a->bones || a->bones > 512 || n_sc
		|| a->n_rot > 4000000 || a->n_tr > 4000000)
		return false;
	a->flags = dm_take (&r, a->bones);
	a->rot = dm_take (&r, (size_t)a->n_rot * 8);
	a->tr = dm_take (&r, (size_t)a->n_tr * 6);
	a->n_idx_rot = dm_u32 (&r);
	a->idx_rot = dm_take (&r, (size_t)a->n_idx_rot * 2);
	a->n_idx_tr = dm_u32 (&r);
	a->idx_tr = dm_take (&r, (size_t)a->n_idx_tr * 2);
	const u32 n6c = dm_u32 (&r);
	if (r.bad || n6c)
		return false;
	// consistency: pool sizes follow from the per-bone flags
	uint rp = 0, tp = 0;
	for (uint b = 0; b < a->bones; b++)
	{
		const u8 fl = a->flags[b];
		if (!(fl & 0x20))
			rp += (fl & 1) ? 1 : a->frames;
		if (!(fl & 0x40))
			tp += (fl & 2) ? 1 : a->frames;
	}
	return (a->n_idx_rot ? rp == a->n_idx_rot : rp == a->n_rot)
		&& (a->n_idx_tr ? tp == a->n_idx_tr : tp == a->n_tr);
}

// Channels in glTF space (Y-up): rotation q' = (-x, -z, y, w) of the stored
// quaternion (the local rotation is the transpose of the stored matrix),
// translation (x, z, -y).
static bool dm_anim_to_glb (const dm_anim_t *a, model_animation_t *out)
{
	memset (out, 0, sizeof (*out));
	snprintf (out->name, sizeof (out->name), "%s", a->name);
	out->channels = CALLOC (a->bones * 2, sizeof (*out->channels));
	if (!out->channels)
		return false;
	const double dt = a->ms_per_frame / 1000.0;
	uint rp = 0, ip = 0;
	for (uint b = 0; b < a->bones; b++)
	{
		const u8 fl = a->flags[b];
		if (!(fl & 0x20))
		{
			const uint n = (fl & 1) ? 1 : a->frames;
			model_anim_channel_t *ch = out->channels + out->num_channels++;
			ch->node_idx = (int)b;
			ch->path = MODEL_ANIM_ROTATION;
			ch->count = n;
			ch->components = 4;
			ch->times = CALLOC (n, sizeof (float));
			ch->values = CALLOC ((size_t)n * 4, sizeof (float));
			if (!ch->times || !ch->values)
				return false;
			for (uint f = 0; f < n; f++)
			{
				uint k = rp + f;
				if (a->n_idx_rot)
					k = a->idx_rot[(size_t)(rp + f) * 2] << 8 | a->idx_rot[(size_t)(rp + f) * 2 + 1];
				if (k >= a->n_rot)
					k = 0;
				const u8 *q = a->rot + (size_t)k * 8;
				const float x = dm_s16 (q) / 16384.0f, y = dm_s16 (q + 2) / 16384.0f,
							z = dm_s16 (q + 4) / 16384.0f, w = dm_s16 (q + 6) / 16384.0f;
				ch->times[f] = (float)(f * dt);
				ch->values[f * 4 + 0] = -x;
				ch->values[f * 4 + 1] = -z;
				ch->values[f * 4 + 2] = y;
				ch->values[f * 4 + 3] = w;
			}
			rp += n;
		}
		if (!(fl & 0x40))
		{
			const uint n = (fl & 2) ? 1 : a->frames;
			model_anim_channel_t *ch = out->channels + out->num_channels++;
			ch->node_idx = (int)b;
			ch->path = MODEL_ANIM_TRANSLATION;
			ch->count = n;
			ch->components = 3;
			ch->times = CALLOC (n, sizeof (float));
			ch->values = CALLOC ((size_t)n * 3, sizeof (float));
			if (!ch->times || !ch->values)
				return false;
			for (uint f = 0; f < n; f++)
			{
				uint k = ip + f;
				if (a->n_idx_tr)
					k = a->idx_tr[(size_t)(ip + f) * 2] << 8 | a->idx_tr[(size_t)(ip + f) * 2 + 1];
				ch->times[f] = (float)(f * dt);
				if (k < a->n_tr)
				{
					const u8 *t = a->tr + (size_t)k * 6;
					ch->values[f * 3 + 0] = dm_s16 (t) * a->tr_scale;
					ch->values[f * 3 + 1] = dm_s16 (t + 4) * a->tr_scale;
					ch->values[f * 3 + 2] = -dm_s16 (t + 2) * a->tr_scale;
				}
			}
			ip += n;
		}
	}
	return true;
}

static void dm_anim_free (model_animation_t *an)
{
	for (size_t i = 0; i < an->num_channels; i++)
	{
		FREE (an->channels[i].times);
		FREE (an->channels[i].values);
	}
	FREE (an->channels);
}

// Bind-pose GLB of one mesh. Coordinates are rotated from the game's Z-up to
// glTF's Y-up: (x, y, z) -> (x, z, -y).
static bool dm_write_glb (
	const dm_mesh_t *m, double scale, ccp name, ccp path, char (*slot_tex)[128], const dm_skel_t *sk,
	const dm_anim_t *anims, uint n_anims)
{
	dm_tri_t *tri = 0;
	uint nt = 0, cap = 0;
	for (uint i = 0; i < m->n_seg; i++)
		if (!dm_expand_dl (m, m->seg + i, &tri, &nt, &cap))
		{
			FREE (tri);
			return false;
		}
	if (!nt)
	{
		FREE (tri);
		return false;
	}

	int slot_mat[DM_SLOTS];
	uint nmat = 0;
	for (uint i = 0; i < DM_SLOTS; i++)
		slot_mat[i] = -1;
	for (uint i = 0; i < nt; i++)
		if (slot_mat[tri[i].slot] < 0)
			slot_mat[tri[i].slot] = (int)nmat++;

	model_t model;
	memset (&model, 0, sizeof (model));
	vec3_t *positions = CALLOC (m->V, sizeof (vec3_t));
	vec3_t *normals = CALLOC (m->V, sizeof (vec3_t));
	vec2_t *texcoords = CALLOC (m->T ? m->T : 1, sizeof (vec2_t));
	mesh_t *meshes = CALLOC (nmat, sizeof (mesh_t));
	model.materials = CALLOC (nmat, sizeof (material_t));
	if (!positions || !normals || !texcoords || !meshes || !model.materials)
	{
		FREE (positions);
		FREE (normals);
		FREE (texcoords);
		FREE (meshes);
		FREE (model.materials);
		FREE (tri);
		return false;
	}
	for (uint i = 0; i < m->V; i++)
	{
		const u8 *v = m->verts + (size_t)i * 16;
		const double inv = scale > 0 ? 1.0 / scale : 1.0 / 4096.0;
		positions[i] = (vec3_t){ (float)(dm_s16 (v) * inv), (float)(dm_s16 (v + 4) * inv),
			(float)(-dm_s16 (v + 2) * inv) };
		const float nx = dm_s8 (v[8]), ny = dm_s8 (v[9]), nz = dm_s8 (v[10]);
		normals[i] = (vec3_t){ nx / 64.0f, nz / 64.0f, -ny / 64.0f };
	}
	for (uint i = 0; i < m->T; i++)
	{
		const u8 *t = m->uv + (size_t)i * 4;
		texcoords[i] = (vec2_t){ (t[0] << 8 | t[1]) / 1024.0f, (t[2] << 8 | t[3]) / 1024.0f };
	}

	// skeleton and skin weights: vertex bones index the inverse-bind table,
	// whose entry k belongs to bone k+1 (bone 0 is the root node); up to three
	// bones with u8 weights at +12..+14, all 0xff / all 0 meaning rigid
	int *position_node = 0;
	if (sk && sk->n && dm_build_joints (&model, sk))
	{
		if (n_anims)
		{
			model.animations = CALLOC (n_anims, sizeof (model_animation_t));
			for (uint i = 0; model.animations && i < n_anims; i++)
				if (dm_anim_to_glb (anims + i, model.animations + model.num_animations))
					model.num_animations++;
				else
					dm_anim_free (model.animations + model.num_animations);
		}
		model.node_influences = CALLOC (m->V, sizeof (node_influence_t));
		position_node = CALLOC (m->V, sizeof (int));
		if (model.node_influences && position_node)
		{
			model.num_node_influences = m->V;
			for (uint i = 0; i < m->V; i++)
			{
				const u8 *v = m->verts + (size_t)i * 16;
				const uint bone[3] = { v[6], v[7], v[11] };
				uint w[3] = { v[12], v[13], v[14] };
				if ((w[0] == 255 && w[1] == 255 && w[2] == 255) || (!w[0] && !w[1] && !w[2]))
					w[0] = 255, w[1] = w[2] = 0;
				node_influence_t *ni = model.node_influences + i;
				ni->weights = CALLOC (3, sizeof (influence_t));
				if (!ni->weights)
					continue;
				for (uint k = 0; k < 3; k++)
				{
					const uint joint = bone[k] + 1;
					if (!w[k] || joint >= sk->n)
						continue;
					ni->weights[ni->num_weights++] = (influence_t){ (int)joint, w[k] / 255.0f };
				}
				if (!ni->num_weights)
					ni->weights[ni->num_weights++] = (influence_t){ 0, 1.0f };
				position_node[i] = (int)i;
			}
		}
		else
		{
			FREE (model.node_influences);
			model.node_influences = 0;
			FREE (position_node);
			position_node = 0;
			FREE (model.joints);
			model.joints = 0;
			model.num_joints = 0;
		}
	}

	// one mesh per material slot (the GLB writer takes one material per mesh);
	// they share the vertex streams
	for (uint slot = 0; slot < DM_SLOTS; slot++)
	{
		if (slot_mat[slot] < 0)
			continue;
		const uint k = (uint)slot_mat[slot];
		mesh_t *mesh = meshes + k;
		uint count = 0;
		for (uint i = 0; i < nt; i++)
			count += tri[i].slot == slot;
		mesh->vertices = CALLOC ((size_t)count * 3, sizeof (vertex_t));
		if (!mesh->vertices)
			continue;
		snprintf (mesh->name, sizeof (mesh->name), "%s_slot%02u", name, slot);
		mesh->positions = positions;
		mesh->normals = normals;
		mesh->texcoords = texcoords;
		mesh->num_positions = mesh->num_normals = m->V;
		mesh->num_texcoords = m->T;
		mesh->material_idx = (int)k;
		mesh->position_node = position_node;
		uint o = 0;
		for (uint i = 0; i < nt; i++)
		{
			if (tri[i].slot != slot)
				continue;
			for (uint e = 0; e < 3; e++, o++)
			{
				vertex_t *vx = mesh->vertices + o;
				vx->position_idx = vx->normal_idx = (int)tri[i].v[e];
				vx->texcoord_idx = (int)tri[i].t[e];
				vx->tangent_idx = -1;
				vx->matrix_idx = -1;
				vx->color_idx[0] = vx->color_idx[1] = -1;
				for (uint x = 0; x < 7; x++)
					vx->extra_texcoord_idx[x] = -1;
			}
		}
		mesh->num_vertices = o;

		material_t *mt = model.materials + k;
		snprintf (mt->name, sizeof (mt->name), "slot%02u", slot);
		mt->diffuse[0] = mt->diffuse[1] = mt->diffuse[2] = mt->diffuse[3] = 1.0f;
		if (slot_tex && slot_tex[slot][0])
		{
			snprintf (mt->textures[0], sizeof (mt->textures[0]), "%s", slot_tex[slot]);
			mt->num_textures = 1;
			mt->wrap_s[0] = mt->wrap_t[0] = 1;
			mt->min_filter[0] = mt->mag_filter[0] = 1;
		}
	}
	model.num_materials = nmat;
	model.meshes = meshes;
	model.num_meshes = nmat;
	const int rc = ExportModelToGLB (&model, path);

	for (uint k = 0; k < nmat; k++)
		FREE (meshes[k].vertices);
	for (size_t i = 0; i < model.num_node_influences; i++)
		FREE (model.node_influences[i].weights);
	FREE (model.node_influences);
	for (size_t i = 0; i < model.num_animations; i++)
		dm_anim_free (model.animations + i);
	FREE (model.animations);
	FREE (model.joints);
	FREE (position_node);
	FREE (meshes);
	FREE (positions);
	FREE (normals);
	FREE (texcoords);
	FREE (model.materials);
	FREE (tri);
	return rc == 0;
}

//-----------------------------------------------------------------------------
// static meshes (class 0x14, loader FUN_80107cf8): positions, texcoords and
// display lists, no skin

typedef struct dm_static_t
{
	uint NP, NU;
	const u8 *pos, *uv;
	struct dm_sdl_t
	{
		uint slot, dl_size;
		const u8 *dl;
	} *dl;
	uint n_dl;
} dm_static_t;

static bool dm_static_parse (dm_static_t *m, const u8 *g, size_t len)
{
	memset (m, 0, sizeof (*m));
	dm_rd_t r = { g, len, 0, false };
	const u8 *flag = dm_take (&r, 1);
	if (!flag || *flag != 1)
		return false;
	m->NP = dm_u32 (&r);
	if (!m->NP || m->NP > DM_MAX_VERTS * 4)
		return false;
	m->pos = dm_take (&r, (size_t)m->NP * 6);
	m->NU = dm_u32 (&r);
	if (r.bad || m->NU > DM_MAX_VERTS * 4)
		return false;
	m->uv = dm_take (&r, (size_t)m->NU * 4);
	const u32 nd = dm_u32 (&r);
	if (r.bad || !nd || nd > 4096)
		return false;
	m->dl = CALLOC (nd, sizeof (*m->dl));
	if (!m->dl)
		return false;
	for (uint i = 0; i < nd && !r.bad; i++)
	{
		const uint slot = dm_u16 (&r);
		dm_u32 (&r); // triangle count
		const u32 sz = dm_u32 (&r);
		const u8 *dl = dm_take (&r, sz);
		if (r.bad)
			break;
		m->dl[m->n_dl++] = (struct dm_sdl_t){ slot, sz, dl };
	}
	if (r.bad || len - r.pos >= 8 || !m->n_dl)
	{
		FREE (m->dl);
		m->dl = 0;
		return false;
	}
	return true;
}

// 0x98 / 0x99 tristrips, 0x90 lists: format 0 vertices are {pos, nrm, tex} u16,
// format 1 vertices {pos u16, colour u8, nrm u16, tex u16}.
static bool dm_expand_static (const dm_static_t *m, dm_tri_t **tri, uint *n, uint *cap)
{
	for (uint di = 0; di < m->n_dl; di++)
	{
		const u8 *d = m->dl[di].dl;
		const size_t len = m->dl[di].dl_size;
		size_t i = 0;
		while (i < len)
		{
			const u8 op = d[i++];
			if (!op)
				continue;
			const u8 prim = op & 0xf8, fmt = op & 7;
			if ((prim != 0x90 && prim != 0x98 && prim != 0xa0) || fmt > 1 || i + 2 > len)
				return false;
			const uint cnt = d[i] << 8 | d[i + 1];
			i += 2;
			const uint stride = fmt ? 7 : 6;
			if (i + (size_t)cnt * stride > len)
				return false;
			uint pv[3], pt[3], have = 0;
			for (uint k = 0; k < cnt; k++, i += stride)
			{
				const u8 *v = d + i;
				const uint p = v[0] << 8 | v[1];
				const uint t = fmt ? (v[5] << 8 | v[6]) : (v[4] << 8 | v[5]);
				if (p >= m->NP || t >= m->NU)
					return false;
				uint a[3], at[3];
				bool emit = false;
				if (prim == 0x90)
				{
					pv[have] = p;
					pt[have++] = t;
					if (have == 3)
					{
						memcpy (a, pv, sizeof (a));
						memcpy (at, pt, sizeof (at));
						have = 0;
						emit = true;
					}
				}
				else if (prim == 0x98)
				{
					if (have < 3)
					{
						pv[have] = p;
						pt[have++] = t;
					}
					else
					{
						pv[0] = pv[1];
						pv[1] = pv[2];
						pv[2] = p;
						pt[0] = pt[1];
						pt[1] = pt[2];
						pt[2] = t;
					}
					if (have == 3)
					{
						const bool odd = (k - 2) & 1;
						a[0] = pv[0];
						a[1] = odd ? pv[2] : pv[1];
						a[2] = odd ? pv[1] : pv[2];
						at[0] = pt[0];
						at[1] = odd ? pt[2] : pt[1];
						at[2] = odd ? pt[1] : pt[2];
						emit = true;
					}
				}
				else
				{
					if (have < 3)
					{
						pv[have] = p;
						pt[have++] = t;
					}
					else
					{
						pv[1] = pv[2];
						pt[1] = pt[2];
						pv[2] = p;
						pt[2] = t;
					}
					if (have == 3)
					{
						memcpy (a, pv, sizeof (a));
						memcpy (at, pt, sizeof (at));
						emit = true;
					}
				}
				if (!emit || a[0] == a[1] || a[1] == a[2] || a[0] == a[2])
					continue;
				if (*n == *cap)
				{
					*cap = *cap ? *cap * 2 : 4096;
					dm_tri_t *nt = REALLOC (*tri, *cap * sizeof (**tri));
					if (!nt)
						return false;
					*tri = nt;
				}
				dm_tri_t *t3 = *tri + (*n)++;
				memcpy (t3->v, a, sizeof (a));
				memcpy (t3->t, at, sizeof (at));
				t3->slot = m->dl[di].slot & 0x7fff;
			}
		}
	}
	return true;
}

static bool dm_write_static_glb (
	const dm_static_t *m, double scale, ccp name, ccp path, char (*slot_tex)[128])
{
	dm_tri_t *tri = 0;
	uint nt = 0, cap = 0;
	if (!dm_expand_static (m, &tri, &nt, &cap) || !nt)
	{
		FREE (tri);
		return false;
	}
	// material slots actually used
	int slot_mat[DM_SLOTS];
	uint nmat = 0;
	for (uint i = 0; i < DM_SLOTS; i++)
		slot_mat[i] = -1;
	for (uint i = 0; i < nt; i++)
	{
		const uint sl = tri[i].slot < DM_SLOTS ? tri[i].slot : DM_SLOTS - 1;
		tri[i].slot = sl;
		if (slot_mat[sl] < 0)
			slot_mat[sl] = (int)nmat++;
	}

	model_t model;
	memset (&model, 0, sizeof (model));
	vec3_t *positions = CALLOC (m->NP, sizeof (vec3_t));
	vec3_t *normals = CALLOC (m->NP, sizeof (vec3_t));
	vec2_t *texcoords = CALLOC (m->NU ? m->NU : 1, sizeof (vec2_t));
	mesh_t *meshes = CALLOC (nmat, sizeof (mesh_t));
	model.materials = CALLOC (nmat, sizeof (material_t));
	if (!positions || !normals || !texcoords || !meshes || !model.materials)
	{
		FREE (positions);
		FREE (normals);
		FREE (texcoords);
		FREE (meshes);
		FREE (model.materials);
		FREE (tri);
		return false;
	}
	const double inv = scale > 0 ? 1.0 / scale : 1.0 / 4096.0;
	for (uint i = 0; i < m->NP; i++)
	{
		const u8 *v = m->pos + (size_t)i * 6;
		positions[i] = (vec3_t){ (float)(dm_s16 (v) * inv), (float)(dm_s16 (v + 4) * inv),
			(float)(-dm_s16 (v + 2) * inv) };
	}
	for (uint i = 0; i < m->NU; i++)
	{
		const u8 *t = m->uv + (size_t)i * 4;
		texcoords[i] = (vec2_t){ (t[0] << 8 | t[1]) / 1024.0f, (t[2] << 8 | t[3]) / 1024.0f };
	}
	// smooth normals from the faces
	for (uint i = 0; i < nt; i++)
	{
		const vec3_t *a = positions + tri[i].v[0], *b = positions + tri[i].v[1],
					 *c = positions + tri[i].v[2];
		const float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z;
		const float vx = c->x - a->x, vy = c->y - a->y, vz = c->z - a->z;
		const vec3_t n = { uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx };
		for (uint k = 0; k < 3; k++)
		{
			vec3_t *d = normals + tri[i].v[k];
			d->x += n.x;
			d->y += n.y;
			d->z += n.z;
		}
	}
	for (uint i = 0; i < m->NP; i++)
	{
		const float l = sqrtf (normals[i].x * normals[i].x + normals[i].y * normals[i].y
			+ normals[i].z * normals[i].z);
		if (l > 0)
		{
			normals[i].x /= l;
			normals[i].y /= l;
			normals[i].z /= l;
		}
		else
			normals[i] = (vec3_t){ 0, 1, 0 };
	}

	for (uint slot = 0; slot < DM_SLOTS; slot++)
	{
		if (slot_mat[slot] < 0)
			continue;
		const uint k = (uint)slot_mat[slot];
		mesh_t *mesh = meshes + k;
		uint count = 0;
		for (uint i = 0; i < nt; i++)
			count += tri[i].slot == slot;
		mesh->vertices = CALLOC ((size_t)count * 3, sizeof (vertex_t));
		if (!mesh->vertices)
			continue;
		snprintf (mesh->name, sizeof (mesh->name), "%s_slot%02u", name, slot);
		mesh->positions = positions;
		mesh->normals = normals;
		mesh->texcoords = texcoords;
		mesh->num_positions = mesh->num_normals = m->NP;
		mesh->num_texcoords = m->NU;
		mesh->material_idx = (int)k;
		uint o = 0;
		for (uint i = 0; i < nt; i++)
		{
			if (tri[i].slot != slot)
				continue;
			for (uint e = 0; e < 3; e++, o++)
			{
				vertex_t *vx = mesh->vertices + o;
				vx->position_idx = vx->normal_idx = (int)tri[i].v[e];
				vx->texcoord_idx = (int)tri[i].t[e];
				vx->tangent_idx = -1;
				vx->matrix_idx = -1;
				vx->color_idx[0] = vx->color_idx[1] = -1;
				for (uint x = 0; x < 7; x++)
					vx->extra_texcoord_idx[x] = -1;
			}
		}
		mesh->num_vertices = o;
		material_t *mt = model.materials + k;
		snprintf (mt->name, sizeof (mt->name), "slot%02u", slot);
		mt->diffuse[0] = mt->diffuse[1] = mt->diffuse[2] = mt->diffuse[3] = 1.0f;
		if (slot_tex && slot_tex[slot][0])
		{
			snprintf (mt->textures[0], sizeof (mt->textures[0]), "%s", slot_tex[slot]);
			mt->num_textures = 1;
			mt->wrap_s[0] = mt->wrap_t[0] = 1;
			mt->min_filter[0] = mt->mag_filter[0] = 1;
		}
	}
	model.num_materials = nmat;
	model.meshes = meshes;
	model.num_meshes = nmat;
	const int rc = ExportModelToGLB (&model, path);
	for (uint k = 0; k < nmat; k++)
		FREE (meshes[k].vertices);
	FREE (meshes);
	FREE (positions);
	FREE (normals);
	FREE (texcoords);
	FREE (model.materials);
	FREE (tri);
	return rc == 0;
}

// Position scale of a mesh object: the base section ends with a 20-byte record
// {f32 radius, f32 units-per-metre, ...} behind a u32 length.
static double dm_base_scale (const dm_ctx_t *c, dm_node_t body)
{
	dm_node_t kids[1];
	if (!dm_children (c, body, kids, 1))
		return 0;
	double scale = 0;
	size_t p = kids[0].start + 12;
	// scan the base section for the u32 length (0x14 or 0x1c) followed by two floats
	for (; p + 12 <= kids[0].end; p += 4)
	{
		const u32 l = rd_be32 (c->d + p);
		if (l != 0x14 && l != 0x1c)
			continue;
		const double a = dm_f32 (c->d + p + 4), b = dm_f32 (c->d + p + 8);
		if (a > 0 && a < 1e6 && b >= 1 && b <= 65536)
			scale = b; // keep the last candidate: the record closes the section
	}
	return scale;
}

// First plausible ASCII name (u32 length 20 + text) inside a subtree.
static bool dm_find_name (const dm_ctx_t *c, dm_node_t n, char *out, size_t cap)
{
	for (size_t p = n.start + 12; p + 8 <= n.end; p += 4)
	{
		if (rd_be32 (c->d + p) != 20)
			continue;
		uint l = 0;
		while (l < 20 && c->d[p + 4 + l] > 0x20 && c->d[p + 4 + l] < 0x7f)
			l++;
		if (l >= 3 && (l == 20 || c->d[p + 4 + l] == 0))
		{
			snprintf (out, cap, "%.*s", (int)l, c->d + p + 4);
			return true;
		}
	}
	return false;
}

typedef struct dm_scene_t
{
	dm_ctx_t c;
	dm_index_t ix;
	bool ok;
} dm_scene_t;

// Texture names are recorded by a dry run of the texture walk; the object
// index maps (class, uid) to the section that defines the object.
static bool dm_scene_open (dm_scene_t *sc, const u8 *d, size_t size, ccp prefix)
{
	memset (sc, 0, sizeof (*sc));
	if (!d || !IsDbkMap (d, size) || !dm_section (d, size, 8, size - 4))
		return false;
	sc->c.d = d;
	sc->c.size = size;
	sc->c.prefix = prefix;
	const dm_node_t root = { 8, rd_be32 (d + 12) };
	dm_walk (&sc->c, root, 0);
	dm_index_walk (&sc->c, &sc->ix, root, 0);
	sc->ok = true;
	return true;
}

static void dm_scene_close (dm_scene_t *sc)
{
	FREE (sc->c.tex);
	FREE (sc->ix.v);
}

typedef struct dm_slot_t
{
	int scene; // -1: no texture, 0: this file, 1: auxiliary file
	uint tex;
} dm_slot_t;

typedef struct dm_model_ctx_t
{
	dm_scene_t *sc[2];
	dm_node_t anc[DM_MAX_DEPTH + 2];
	ccp dest; // NULL: plan only (mark wanted auxiliary textures)
	u8 *want_aux;
	uint written;
} dm_model_ctx_t;

static const dm_obj_t *dm_lookup (dm_model_ctx_t *mc, int cls, u32 ua, u32 ub, int *scene)
{
	for (int s = 0; s < 2; s++)
	{
		if (!mc->sc[s])
			continue;
		const dm_obj_t *o = dm_index_find (&mc->sc[s]->ix, cls, ua, ub);
		if (o)
		{
			*scene = s;
			return o;
		}
	}
	return 0;
}

// Texture of each material slot of the model object MODEL: slot i is entry i
// of the model's material set; a material's first class-12 reference is its
// texture. Material and texture may live in the auxiliary file (the shared
// game database next to the maps).
static void dm_resolve_slots (dm_model_ctx_t *mc, dm_node_t model, dm_slot_t *slots)
{
	dm_scene_t *own = mc->sc[0];
	const dm_obj_t *set = 0;
	for (uint i = 0; i < own->ix.used && !set; i++)
		if (own->ix.v[i].cls == 0x25 && own->ix.v[i].start > model.start && own->ix.v[i].start < model.end)
			set = own->ix.v + i;
	for (uint i = 0; i < DM_SLOTS; i++)
		slots[i].scene = -1;
	if (!set)
		return;
	dm_ref_t refs[DM_MAX_SET];
	const uint n = dm_material_set (&own->c, (dm_node_t){ set->start, set->end }, refs);
	for (uint slot = 0; slot < n && slot < DM_SLOTS; slot++)
	{
		int ms = 0;
		const dm_obj_t *mat = dm_lookup (mc, refs[slot].cls, refs[slot].ua, refs[slot].ub, &ms);
		if (!mat)
			continue;
		dm_ref_t t;
		if (!dm_first_ref (&mc->sc[ms]->c, (dm_node_t){ mat->start, mat->end }, 12, 0, &t))
			continue;
		int ts = 0;
		const dm_obj_t *to = dm_lookup (mc, 12, t.ua, t.ub, &ts);
		if (!to)
			continue;
		const dm_ctx_t *tc = &mc->sc[ts]->c;
		for (uint k = 0; k < tc->textures && k < tc->tex_cap; k++)
			if (tc->tex[k].obj_start == to->start)
			{
				slots[slot].scene = ts;
				slots[slot].tex = k;
				break;
			}
	}
}

static void dm_slot_names (dm_model_ctx_t *mc, const dm_slot_t *slots, char (*slot_tex)[128])
{
	for (uint i = 0; i < DM_SLOTS; i++)
	{
		slot_tex[i][0] = 0;
		if (slots[i].scene < 0)
			continue;
		const dm_scene_t *sc = mc->sc[slots[i].scene];
		snprintf (slot_tex[i], 128, "../%s.png", sc->c.tex[slots[i].tex].name);
	}
}

static void dm_walk_models (dm_model_ctx_t *mc, dm_node_t parent, dm_node_t n, uint depth)
{
	dm_ctx_t *c = &mc->sc[0]->c;
	if (depth > DM_MAX_DEPTH || mc->written >= DM_MAX_TEXTURES)
		return;
	mc->anc[depth] = n;
	// leaf test: no nested sections
	dm_node_t first[1];
	const uint nk = dm_children (c, n, first, 1);
	if (!nk && n.end - n.start > 64 && c->d[n.start + 12] == 1)
	{
		dm_mesh_t m;
		dm_static_t sm;
		const u8 *leaf = c->d + n.start + 12;
		const size_t leaf_len = n.end - n.start - 12;
		const bool skinned = dm_mesh_parse (&m, leaf, leaf_len);
		const bool statik = !skinned && dm_static_parse (&sm, leaf, leaf_len);
		if (skinned || statik)
		{
			dm_slot_t slots[DM_SLOTS];
			for (uint i = 0; i < DM_SLOTS; i++)
				slots[i].scene = -1;
			if (depth >= 2)
				dm_resolve_slots (mc, mc->anc[depth - 2], slots);
			bool used[DM_SLOTS] = { false };
			if (skinned)
				for (uint i = 0; i < m.n_seg; i++)
					used[m.seg[i].slot] = true;
			else
				for (uint i = 0; i < sm.n_dl; i++)
					used[(sm.dl[i].slot & 0x7fff) < DM_SLOTS ? (sm.dl[i].slot & 0x7fff) : DM_SLOTS - 1]
						= true;
			for (uint i = 0; i < DM_SLOTS; i++)
				if (!used[i])
					slots[i].scene = -1;
			if (!mc->dest)
			{
				// plan: remember which auxiliary textures the models use
				for (uint i = 0; i < DM_SLOTS; i++)
					if (slots[i].scene == 1)
						mc->want_aux[slots[i].tex] = 1;
			}
			else
			{
				char name[64] = "";
				dm_find_name (c, parent, name, sizeof (name));
				char clean[64];
				uint k = 0;
				for (ccp p = name; *p && k + 1 < sizeof (clean); p++)
					clean[k++] = (isalnum ((u8)*p) || *p == '_' || *p == '-' || *p == '.') ? *p : '_';
				clean[k] = 0;
				if (!k)
					snprintf (clean, sizeof (clean), "model");
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/models/%04u_%s.glb", mc->dest, mc->written, clean);
				char slot_tex[DM_SLOTS][128];
				dm_slot_names (mc, slots, slot_tex);
				const double scale = dm_base_scale (c, parent);
				bool ok = false;
				if (!CreatePath (path, false))
				{
					if (skinned)
					{
						dm_skel_t sk;
						const bool have_sk = dm_skel_parse (c, parent, &sk);
						dm_anim_t *anims = 0;
						uint n_anims = 0;
						if (have_sk)
						{
							// animations of this file with one channel set per bone whose
							// name starts with the root bone's name
							char root[24];
							snprintf (root, sizeof (root), "%s", sk.b[0].name);
							dm_index_t *ix = &mc->sc[0]->ix;
							for (uint i = 0; i < ix->used; i++)
							{
								if (ix->v[i].cls != 0x2e)
									continue;
								const dm_node_t an = { ix->v[i].start, ix->v[i].end };
								dm_node_t ak[1];
								size_t gs = an.start + 12, best = 0, best_len = 0, q = gs;
								(void)ak;
								while (q + 4 <= an.end)
								{
									if (q + 12 <= an.end && dm_section (c->d, c->size, q, an.end))
									{
										if (q - gs > best_len)
											best = gs, best_len = q - gs;
										gs = rd_be32 (c->d + q + 4) + 4;
										q = gs;
									}
									else
										q += 4;
								}
								if (an.end - gs > best_len)
									best = gs, best_len = an.end - gs;
								if (best_len < 40)
									continue;
								dm_anim_t a;
								if (!dm_anim_parse (c->d + best, best_len, &a) || a.bones != sk.n)
									continue;
								char an_name[64] = "";
								dm_find_name (c, an, an_name, sizeof (an_name));
								snprintf (a.name, sizeof (a.name), "%s", an_name[0] ? an_name : "anim");
								// prefix test, case-insensitive
								const size_t rl = strlen (root);
								if (strncasecmp (a.name, root, rl))
									continue;
								if (n_anims >= 64)
									break;
								dm_anim_t *na = REALLOC (anims, (n_anims + 1) * sizeof (*na));
								if (!na)
									break;
								anims = na;
								anims[n_anims++] = a;
							}
						}
						ok = dm_write_glb (&m, scale, clean, path, slot_tex, have_sk ? &sk : 0, anims, n_anims);
						FREE (anims);
						if (have_sk)
							FREE (sk.b);
					}
					else
						ok = dm_write_static_glb (&sm, scale, clean, path, slot_tex);
				}
				if (ok)
					mc->written++;
			}
			if (skinned)
				FREE (m.seg);
			else
				FREE (sm.dl);
		}
		return;
	}
	size_t p = n.start + 12;
	while (p + 12 <= n.end)
	{
		if (dm_section (c->d, c->size, p, n.end))
		{
			const dm_node_t k = { p, rd_be32 (c->d + p + 4) };
			dm_walk_models (mc, n, k, depth + 1);
			p = k.end + 4;
		}
		else
			p += 4;
	}
}

enumError ScanDbkMap (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *d, size_t size,
	const u8 *aux, size_t aux_size)
{
	if (!entries || !n_entries || !IsDbkMap (d, size))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	dm_out_t out = { 0, 0, 0 };
	dm_ctx_t c = { d, size, &out, 0, 0, 0, 0, 0 };
	const dm_node_t root = { 8, dm_section (d, size, 8, size - 4) ? rd_be32 (d + 12) : 0 };
	if (root.end)
		dm_walk (&c, root, 0);
	FREE (c.tex);

	// Textures of the shared game database that this file's models use.
	dm_scene_t own, shared;
	if (root.end && aux && dm_scene_open (&own, d, size, 0))
	{
		if (dm_scene_open (&shared, aux, aux_size, "gam_") && shared.c.textures)
		{
			dm_model_ctx_t mc;
			memset (&mc, 0, sizeof (mc));
			mc.sc[0] = &own;
			mc.sc[1] = &shared;
			mc.want_aux = CALLOC (shared.c.textures, 1);
			if (mc.want_aux)
			{
				dm_walk_models (&mc, root, root, 0);
				dm_ctx_t ac = { aux, aux_size, &out, 0, 0, 0, mc.want_aux, "gam_" };
				dm_walk (&ac, (dm_node_t){ 8, rd_be32 (aux + 12) }, 0);
				FREE (ac.tex);
				FREE (mc.want_aux);
			}
		}
		dm_scene_close (&shared);
		dm_scene_close (&own);
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

uint ExportDbkMapModels (const u8 *d, size_t size, const u8 *aux, size_t aux_size, ccp dest_dir)
{
	dm_scene_t own, shared;
	if (!dm_scene_open (&own, d, size, 0))
		return 0;
	dm_model_ctx_t mc;
	memset (&mc, 0, sizeof (mc));
	mc.sc[0] = &own;
	// auxiliary textures were written as "gam_" members by ScanDbkMap
	const bool have_aux = aux && dm_scene_open (&shared, aux, aux_size, "gam_");
	if (have_aux)
		mc.sc[1] = &shared;
	mc.dest = dest_dir;
	const dm_node_t root = { 8, rd_be32 (d + 12) };
	dm_walk_models (&mc, root, root, 0);
	if (have_aux)
		dm_scene_close (&shared);
	dm_scene_close (&own);
	return mc.written;
}
