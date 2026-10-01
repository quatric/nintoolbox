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

// Bind-pose GLB of one mesh. Coordinates are rotated from the game's Z-up to
// glTF's Y-up: (x, y, z) -> (x, z, -y).
static bool dm_write_glb (const dm_mesh_t *m, ccp name, ccp path)
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
	mesh_t mesh;
	memset (&mesh, 0, sizeof (mesh));
	snprintf (mesh.name, sizeof (mesh.name), "%s", name);
	mesh.positions = CALLOC (m->V, sizeof (vec3_t));
	mesh.normals = CALLOC (m->V, sizeof (vec3_t));
	mesh.texcoords = CALLOC (m->T ? m->T : 1, sizeof (vec2_t));
	mesh.vertices = CALLOC ((size_t)nt * 3, sizeof (vertex_t));
	mesh.triangle_materials = CALLOC (nt, sizeof (int));
	model.materials = CALLOC (nmat, sizeof (material_t));
	if (!mesh.positions || !mesh.normals || !mesh.texcoords || !mesh.vertices
		|| !mesh.triangle_materials || !model.materials)
	{
		FREE (mesh.positions);
		FREE (mesh.normals);
		FREE (mesh.texcoords);
		FREE (mesh.vertices);
		FREE (mesh.triangle_materials);
		FREE (model.materials);
		FREE (tri);
		return false;
	}
	for (uint i = 0; i < m->V; i++)
	{
		const u8 *v = m->verts + (size_t)i * 16;
		mesh.positions[i] = (vec3_t){ dm_s16 (v) / 4096.0f, dm_s16 (v + 4) / 4096.0f,
			-dm_s16 (v + 2) / 4096.0f };
		const float nx = dm_s8 (v[8]), ny = dm_s8 (v[9]), nz = dm_s8 (v[10]);
		mesh.normals[i] = (vec3_t){ nx / 64.0f, nz / 64.0f, -ny / 64.0f };
	}
	mesh.num_positions = mesh.num_normals = m->V;
	for (uint i = 0; i < m->T; i++)
	{
		const u8 *t = m->uv + (size_t)i * 4;
		mesh.texcoords[i]
			= (vec2_t){ (t[0] << 8 | t[1]) / 1024.0f, (t[2] << 8 | t[3]) / 1024.0f };
	}
	mesh.num_texcoords = m->T;
	for (uint i = 0; i < nt; i++)
	{
		for (uint k = 0; k < 3; k++)
		{
			vertex_t *vx = mesh.vertices + (size_t)i * 3 + k;
			vx->position_idx = vx->normal_idx = (int)tri[i].v[k];
			vx->texcoord_idx = (int)tri[i].t[k];
			vx->tangent_idx = -1;
			vx->matrix_idx = -1;
			vx->color_idx[0] = vx->color_idx[1] = -1;
			for (uint e = 0; e < 7; e++)
				vx->extra_texcoord_idx[e] = -1;
		}
		mesh.triangle_materials[i] = slot_mat[tri[i].slot];
	}
	mesh.num_vertices = (size_t)nt * 3;
	mesh.material_idx = 0;

	for (uint i = 0; i < DM_SLOTS; i++)
		if (slot_mat[i] >= 0)
		{
			material_t *mt = model.materials + slot_mat[i];
			snprintf (mt->name, sizeof (mt->name), "slot%02u", i);
			mt->diffuse[0] = mt->diffuse[1] = mt->diffuse[2] = mt->diffuse[3] = 1.0f;
		}
	model.num_materials = nmat;
	model.meshes = &mesh;
	model.num_meshes = 1;
	const int rc = ExportModelToGLB (&model, path);

	FREE (mesh.positions);
	FREE (mesh.normals);
	FREE (mesh.texcoords);
	FREE (mesh.vertices);
	FREE (mesh.triangle_materials);
	FREE (model.materials);
	FREE (tri);
	return rc == 0;
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

typedef struct dm_model_ctx_t
{
	dm_ctx_t c;
	ccp dest;
	uint written;
} dm_model_ctx_t;

static void dm_walk_models (dm_model_ctx_t *mc, dm_node_t parent, dm_node_t n, uint depth)
{
	dm_ctx_t *c = &mc->c;
	if (depth > DM_MAX_DEPTH || mc->written >= DM_MAX_TEXTURES)
		return;
	// leaf test: no nested sections
	dm_node_t first[1];
	const uint nk = dm_children (c, n, first, 1);
	if (!nk && n.end - n.start > 64 && c->d[n.start + 12] == 1)
	{
		dm_mesh_t m;
		if (dm_mesh_parse (&m, c->d + n.start + 12, n.end - n.start - 12))
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
			if (!CreatePath (path, false) && dm_write_glb (&m, clean, path))
				mc->written++;
			FREE (m.seg);
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

uint ExportDbkMapModels (const u8 *d, size_t size, ccp dest_dir)
{
	if (!IsDbkMap (d, size) || !dm_section (d, size, 8, size - 4))
		return 0;
	dm_model_ctx_t mc;
	memset (&mc, 0, sizeof (mc));
	mc.c.d = d;
	mc.c.size = size;
	mc.dest = dest_dir;
	const dm_node_t root = { 8, rd_be32 (d + 12) };
	dm_walk_models (&mc, root, root, 0);
	return mc.written;
}
