// SPDX-License-Identifier: GPL-2.0+
// Granny 2 .gr2 reader (big-endian, 32-bit pointers, uncompressed sections).
#include "lib-granny.h"
#include "lib-model-glb.h"
#include "lib-std.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#undef calloc
#undef realloc
#undef free

static const u8 gr_magic[16] = { 0xb5, 0x95, 0x11, 0x0e, 0x4b, 0xb5, 0xa5, 0x6a, 0x50, 0x28, 0x28, 0xeb, 0x04, 0xb3, 0x78, 0x25 };

bool IsGrannyBE (const u8 *d, uint size)
{
	return d && size >= 0x68 && !memcmp (d, gr_magic, 16);
}

typedef struct { u32 off, tsec, toff; } reloc_t;

typedef struct
{
	const u8 *data;
	u32 size;
	reloc_t *rel;
	u32 nrel;
} gsec_t;

typedef struct gdef gdef_t;
struct gdef
{
	uint type, aw;
	char *name;
	int rsec;
	u32 roff;
	bool has_ref;
};

typedef struct
{
	int sec;
	u32 off;
	gdef_t *defs;
	uint n;
	uint size;
} gtype_t;

typedef struct
{
	const u8 *file;
	uint fsize;
	gsec_t sec[32];
	uint nsec;
	gtype_t types[512];
	uint ntypes;
	bool bad;
} gfile_t;

// value tree
typedef enum { GV_NULL, GV_INT, GV_FLOAT, GV_STR, GV_ARR, GV_OBJ } gv_kind_t;
typedef struct gv gv_t;
struct gv
{
	gv_kind_t kind;
	double num;
	const char *str;
	gv_t *items;  // GV_ARR elements / GV_OBJ member values
	char **keys;  // GV_OBJ member names (shared with the type cache)
	size_t n;
};

static int rel_cmp (const void *a, const void *b)
{
	const reloc_t *x = a, *y = b;
	return x->off < y->off ? -1 : x->off > y->off;
}

static bool g_u32 (const gfile_t *g, int sec, u32 off, u32 *out)
{
	if (sec < 0 || (uint)sec >= g->nsec || (size_t)off + 4 > g->sec[sec].size)
		return false;
	*out = rd_be32 (g->sec[sec].data + off);
	return true;
}

static bool g_ptr (const gfile_t *g, int sec, u32 off, int *tsec, u32 *toff)
{
	if (sec < 0 || (uint)sec >= g->nsec)
		return false;
	const gsec_t *s = g->sec + sec;
	size_t lo = 0, hi = s->nrel;
	while (lo < hi)
	{
		const size_t mid = (lo + hi) / 2;
		if (s->rel[mid].off == off)
		{
			*tsec = (int)s->rel[mid].tsec;
			*toff = s->rel[mid].toff;
			return (uint)*tsec < g->nsec;
		}
		if (s->rel[mid].off < off)
			lo = mid + 1;
		else
			hi = mid;
	}
	return false;
}

static const char *g_cstr (const gfile_t *g, int sec, u32 off)
{
	if (sec < 0 || (uint)sec >= g->nsec || off >= g->sec[sec].size)
		return 0;
	const u8 *p = g->sec[sec].data + off, *e = memchr (p, 0, g->sec[sec].size - off);
	return e ? (const char *)p : 0;
}

static const gtype_t *g_type (gfile_t *g, int sec, u32 off);

static uint def_size (gfile_t *g, const gdef_t *m)
{
	const uint aw = m->aw ? m->aw : 1;
	switch (m->type)
	{
	case 1:
	{
		const gtype_t *t = m->has_ref ? g_type (g, m->rsec, m->roff) : 0;
		return t ? t->size * aw : 0;
	}
	case 2: return 4;
	case 3: case 4: case 5: return 8;
	case 7: return 12;
	case 6: return 0;
	case 8: case 22: return 4;
	case 9: return 68;
	case 10: case 19: case 20: return 4 * aw;
	case 11: case 12: case 13: case 14: return aw;
	case 15: case 16: case 17: case 18: case 21: return 2 * aw;
	}
	return 0;
}

static const gtype_t *g_type (gfile_t *g, int sec, u32 off)
{
	for (uint i = 0; i < g->ntypes; i++)
		if (g->types[i].sec == sec && g->types[i].off == off)
			return g->types + i;
	if (g->ntypes >= 512 || g->bad)
		return 0;
	gtype_t *t = g->types + g->ntypes;
	memset (t, 0, sizeof (*t));
	t->sec = sec;
	t->off = off;
	uint cap = 8;
	t->defs = calloc (cap, sizeof (*t->defs));
	g->ntypes++; // reserve the slot before recursing (types may refer to themselves)
	if (!t->defs)
	{
		g->bad = true;
		return 0;
	}
	for (u32 o = off;; o += 32)
	{
		u32 type, aw;
		if (!g_u32 (g, sec, o, &type) || !g_u32 (g, sec, o + 12, &aw) || type > 22)
		{
			g->bad = true;
			return 0;
		}
		if (!type)
			break;
		if (t->n == cap)
		{
			cap *= 2;
			gdef_t *nd = realloc (t->defs, cap * sizeof (*nd));
			if (!nd)
			{
				g->bad = true;
				return 0;
			}
			t->defs = nd;
		}
		gdef_t *d = t->defs + t->n++;
		memset (d, 0, sizeof (*d));
		d->type = type;
		d->aw = aw;
		int ns;
		u32 no;
		const char *nm = g_ptr (g, sec, o + 4, &ns, &no) ? g_cstr (g, ns, no) : 0;
		{
			const char *src = nm ? nm : "";
			d->name = calloc (strlen (src) + 1, 1);
			if (d->name)
				memcpy (d->name, src, strlen (src));
		}
		d->has_ref = g_ptr (g, sec, o + 8, &d->rsec, &d->roff);
	}
	uint sz = 0;
	for (uint i = 0; i < t->n; i++)
		sz += def_size (g, t->defs + i);
	t->size = sz;
	return t;
}

static void gv_free (gv_t *v)
{
	if (!v)
		return;
	if (v->kind == GV_ARR || v->kind == GV_OBJ)
		for (size_t i = 0; i < v->n; i++)
			gv_free (v->items + i);
	free (v->items);
	free (v->keys);
	v->items = 0;
	v->keys = 0;
	v->n = 0;
}

static bool g_obj (gfile_t *g, int sec, u32 off, const gtype_t *t, int depth, gv_t *out);

static bool g_val (gfile_t *g, int sec, u32 off, const gdef_t *m, int depth, gv_t *out)
{
	const uint aw = m->aw ? m->aw : 1;
	memset (out, 0, sizeof (*out));
	const u8 *base = (uint)sec < g->nsec ? g->sec[sec].data : 0;
	const u32 ssz = (uint)sec < g->nsec ? g->sec[sec].size : 0;
	int rs;
	u32 ro;
	switch (m->type)
	{
	case 1: // inline struct (array when aw > 1)
	{
		const gtype_t *t = m->has_ref ? g_type (g, m->rsec, m->roff) : 0;
		if (!t)
			return !g->bad;
		if (m->aw)
		{
			out->kind = GV_ARR;
			out->items = calloc (aw, sizeof (gv_t));
			if (!out->items)
				return false;
			out->n = aw;
			for (uint i = 0; i < aw; i++)
				if (!g_obj (g, sec, off + i * t->size, t, depth + 1, out->items + i))
					return false;
			return true;
		}
		return g_obj (g, sec, off, t, depth + 1, out);
	}
	case 2: // reference
	{
		const gtype_t *t = m->has_ref ? g_type (g, m->rsec, m->roff) : 0;
		if (!t || !g_ptr (g, sec, off, &rs, &ro))
			return !g->bad;
		return g_obj (g, rs, ro, t, depth + 1, out);
	}
	case 3: // reference to array {count, ptr}
	case 4: // array of references
	case 7: // reference to variant array {type ptr, count, ptr}
	{
		u32 n, cnt_off = m->type == 7 ? off + 4 : off, ptr_off = cnt_off + 4;
		const gtype_t *t = m->has_ref ? g_type (g, m->rsec, m->roff) : 0;
		if (m->type == 7)
		{
			int ts;
			u32 to;
			t = g_ptr (g, sec, off, &ts, &to) ? g_type (g, ts, to) : 0;
		}
		if (!g_u32 (g, sec, cnt_off, &n) || !n || !g_ptr (g, sec, ptr_off, &rs, &ro) || !t || n > 4000000)
			return !g->bad;
		out->kind = GV_ARR;
		out->items = calloc (n, sizeof (gv_t));
		if (!out->items)
			return false;
		out->n = n;
		for (u32 i = 0; i < n; i++)
		{
			if (m->type == 4)
			{
				int ts;
				u32 to;
				if (g_ptr (g, rs, ro + 4 * i, &ts, &to) && !g_obj (g, ts, to, t, depth + 1, out->items + i))
					return false;
			}
			else if (!g_obj (g, rs, ro + i * t->size, t, depth + 1, out->items + i))
				return false;
		}
		return true;
	}
	case 8: // string
		if (g_ptr (g, sec, off, &rs, &ro))
		{
			out->str = g_cstr (g, rs, ro);
			out->kind = out->str ? GV_STR : GV_NULL;
		}
		return true;
	case 9: // transform: flags, translation[3], orientation[4], scale/shear[9]
		if ((size_t)off + 68 > ssz)
			return true;
		out->kind = GV_ARR;
		out->items = calloc (17, sizeof (gv_t));
		if (!out->items)
			return false;
		out->n = 17;
		for (int i = 0; i < 17; i++)
		{
			out->items[i].kind = i ? GV_FLOAT : GV_INT;
			if (i)
			{
				u32 u = rd_be32 (base + off + 4 * i);
				float f;
				memcpy (&f, &u, 4);
				out->items[i].num = f;
			}
			else
				out->items[i].num = rd_be32 (base + off);
		}
		return true;
	case 5: case 6: case 22: case 21:
		return true;
	}
	// scalar arrays
	uint esz = m->type == 10 || m->type == 19 || m->type == 20 ? 4 : m->type >= 15 && m->type <= 18 ? 2 : 1;
	if ((size_t)off + (size_t)esz * aw > ssz)
		return true;
	gv_t *dst = out;
	if (m->aw)
	{
		out->kind = GV_ARR;
		out->items = calloc (aw, sizeof (gv_t));
		if (!out->items)
			return false;
		out->n = aw;
	}
	for (uint i = 0; i < aw; i++)
	{
		if (m->aw)
			dst = out->items + i;
		const u8 *p = base + off + (size_t)i * esz;
		switch (m->type)
		{
		case 10:
		{
			u32 u = rd_be32 (p);
			float f;
			memcpy (&f, &u, 4);
			dst->kind = GV_FLOAT;
			dst->num = f;
			break;
		}
		case 11: case 13: dst->kind = GV_INT; dst->num = (int8_t)p[0]; break;
		case 12: case 14: dst->kind = GV_INT; dst->num = p[0]; break;
		case 15: case 17: dst->kind = GV_INT; dst->num = (int16_t)rd_be16 (p); break;
		case 16: case 18: dst->kind = GV_INT; dst->num = rd_be16 (p); break;
		case 19: dst->kind = GV_INT; dst->num = (int32_t)rd_be32 (p); break;
		case 20: dst->kind = GV_INT; dst->num = rd_be32 (p); break;
		}
	}
	return true;
}

static bool g_obj (gfile_t *g, int sec, u32 off, const gtype_t *t, int depth, gv_t *out)
{
	memset (out, 0, sizeof (*out));
	if (depth > 12 || g->bad)
		return true;
	out->kind = GV_OBJ;
	out->n = t->n;
	out->items = calloc (t->n ? t->n : 1, sizeof (gv_t));
	out->keys = calloc (t->n ? t->n : 1, sizeof (char *));
	if (!out->items || !out->keys)
		return false;
	u32 cur = off;
	for (uint i = 0; i < t->n; i++)
	{
		out->keys[i] = t->defs[i].name;
		if (!g_val (g, sec, cur, t->defs + i, depth, out->items + i))
			return false;
		cur += def_size (g, t->defs + i);
	}
	return true;
}

static const gv_t *gv_get (const gv_t *o, const char *key)
{
	if (!o || o->kind != GV_OBJ)
		return 0;
	for (size_t i = 0; i < o->n; i++)
		if (!strcmp (o->keys[i], key))
			return o->items + i;
	return 0;
}

static double gv_num (const gv_t *v, size_t i, double def)
{
	if (!v)
		return def;
	if (v->kind == GV_ARR)
		return i < v->n ? gv_num (v->items + i, 0, def) : def;
	return v->kind == GV_INT || v->kind == GV_FLOAT ? v->num : def;
}

static bool gv_has (const gv_t *v) { return v && v->kind != GV_NULL; }

static bool g_open (gfile_t *g, const u8 *d, uint size)
{
	memset (g, 0, sizeof (*g));
	g->file = d;
	g->fsize = size;
	if (!IsGrannyBE (d, size))
		return false;
	const u32 so = rd_be32 (d + 0x2c), ns = rd_be32 (d + 0x30);
	if (ns == 0 || ns > 32 || (size_t)0x20 + so + (size_t)ns * 44 > size)
		return false;
	g->nsec = ns;
	for (uint i = 0; i < ns; i++)
	{
		const u8 *h = d + 0x20 + so + (size_t)i * 44;
		const u32 comp = rd_be32 (h), doff = rd_be32 (h + 4), dsz = rd_be32 (h + 8), roff = rd_be32 (h + 28),
				  nrel = rd_be32 (h + 32);
		if (comp != 0 || (size_t)doff + dsz > size || (size_t)roff + (size_t)nrel * 12 > size || nrel > 4000000)
			return false;
		gsec_t *s = g->sec + i;
		s->data = d + doff;
		s->size = dsz;
		s->nrel = nrel;
		s->rel = calloc (nrel ? nrel : 1, sizeof (reloc_t));
		if (!s->rel)
			return false;
		for (u32 k = 0; k < nrel; k++)
		{
			const u8 *r = d + roff + (size_t)k * 12;
			s->rel[k].off = rd_be32 (r);
			s->rel[k].tsec = rd_be32 (r + 4);
			s->rel[k].toff = rd_be32 (r + 8);
		}
		qsort (s->rel, nrel, sizeof (reloc_t), rel_cmp);
	}
	return true;
}

static void g_close (gfile_t *g)
{
	for (uint i = 0; i < g->nsec; i++)
		free (g->sec[i].rel);
	for (uint i = 0; i < g->ntypes; i++)
	{
		for (uint k = 0; k < g->types[i].n; k++)
			free (g->types[i].defs[k].name);
		free (g->types[i].defs);
	}
}

// ---- GLB conversion ---------------------------------------------------------

static void stem_of (const char *path, char *out, size_t sz)
{
	const char *b = path;
	for (const char *q = path; *q; q++)
		if (*q == '/' || *q == '\\')
			b = q + 1;
	snprintf (out, sz, "%s", b);
	char *dot = strrchr (out, '.');
	if (dot)
		*dot = 0;
}

static const char *material_texture (const gv_t *mat)
{
	// diffuse map's texture file name, else the material's own texture
	const gv_t *maps = gv_get (mat, "Maps");
	if (maps && maps->kind == GV_ARR)
		for (size_t i = 0; i < maps->n; i++)
		{
			const gv_t *use = gv_get (maps->items + i, "Usage");
			if (use && use->kind == GV_STR && !strncasecmp (use->str, "diffuse", 7))
			{
				const gv_t *tex = gv_get (gv_get (maps->items + i, "Map"), "Texture");
				const gv_t *fn = gv_get (tex, "FromFileName");
				if (fn && fn->kind == GV_STR)
					return fn->str;
			}
		}
	const gv_t *fn = gv_get (gv_get (mat, "Texture"), "FromFileName");
	return fn && fn->kind == GV_STR ? fn->str : 0;
}

enumError DecodeGrannyBE (const u8 *data, uint size, ccp out_path)
{
	if (!out_path)
		return ERR_INVALID_DATA;
	gfile_t g;
	if (!g_open (&g, data, size))
	{
		g_close (&g);
		return ERR_INVALID_DATA;
	}
	int rts = (int)rd_be32 (data + 0x34), ros = (int)rd_be32 (data + 0x3c);
	const gtype_t *rt = g_type (&g, rts, rd_be32 (data + 0x38));
	gv_t root;
	memset (&root, 0, sizeof (root));
	enumError err = ERR_INVALID_DATA;
	model_t *model = 0;
	char dir[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", out_path);
	char *sl = strrchr (dir, '/');
	if (sl)
		*sl = 0;
	else
		snprintf (dir, sizeof (dir), ".");
	if (!rt || !g_obj (&g, ros, rd_be32 (data + 0x40), rt, 0, &root) || g.bad)
		goto done;
	const gv_t *meshes = gv_get (&root, "Meshes");
	if (!meshes || meshes->kind != GV_ARR)
		goto done;
	model = calloc (1, sizeof (*model));
	if (!model)
		goto done;
	for (size_t mi = 0; mi < meshes->n; mi++)
	{
		const gv_t *mesh = meshes->items + mi;
		const gv_t *verts = gv_get (gv_get (mesh, "PrimaryVertexData"), "Vertices");
		const gv_t *topo = gv_get (mesh, "PrimaryTopology");
		const gv_t *groups = gv_get (topo, "Groups");
		const gv_t *i32 = gv_get (topo, "Indices"), *i16 = gv_get (topo, "Indices16");
		const gv_t *mname = gv_get (mesh, "Name");
		const gv_t *binds = gv_get (mesh, "MaterialBindings");
		if (!verts || verts->kind != GV_ARR || !verts->n || !topo)
			continue;
		const bool use32 = i32 && i32->kind == GV_ARR && i32->n;
		const gv_t *idx = use32 ? i32 : i16;
		if (!idx || idx->kind != GV_ARR || !idx->n)
			continue;
		// one mesh_t per material group (or one for all triangles)
		const size_t ngroups = groups && groups->kind == GV_ARR && groups->n ? groups->n : 1;
		for (size_t gi = 0; gi < ngroups; gi++)
		{
			size_t first = 0, count = idx->n / 3;
			int matidx = -1;
			if (groups && groups->kind == GV_ARR && groups->n)
			{
				first = (size_t)gv_num (gv_get (groups->items + gi, "TriFirst"), 0, 0);
				count = (size_t)gv_num (gv_get (groups->items + gi, "TriCount"), 0, 0);
				matidx = (int)gv_num (gv_get (groups->items + gi, "MaterialIndex"), 0, -1);
			}
			if (!count || (first + count) * 3 > idx->n)
				continue;
			mesh_t *grown = realloc (model->meshes, (model->num_meshes + 1) * sizeof (*grown));
			if (!grown)
				goto done;
			model->meshes = grown;
			mesh_t *m = grown + model->num_meshes++;
			memset (m, 0, sizeof (*m));
			snprintf (m->name, sizeof (m->name), "%s%s%zu", mname && mname->kind == GV_STR ? mname->str : "mesh",
				ngroups > 1 ? "_g" : "", gi);
			m->num_positions = m->num_normals = m->num_texcoords = verts->n;
			m->positions = calloc (verts->n, sizeof (vec3_t));
			m->normals = calloc (verts->n, sizeof (vec3_t));
			m->texcoords = calloc (verts->n, sizeof (vec2_t));
			m->num_colors[0] = verts->n;
			m->colors[0] = calloc (verts->n, sizeof (color4_t));
			m->vertices = calloc (count * 3, sizeof (vertex_t));
			if (!m->positions || !m->normals || !m->texcoords || !m->colors[0] || !m->vertices)
				goto done;
			bool have_color = false;
			for (size_t vi = 0; vi < verts->n; vi++)
			{
				const gv_t *v = verts->items + vi;
				const gv_t *pos = gv_get (v, "Position"), *nrm = gv_get (v, "Normal"), *uv = gv_get (v, "TextureCoordinates0"),
						   *col = gv_get (v, "DiffuseColor0");
				m->positions[vi] = (vec3_t){ (float)gv_num (pos, 0, 0), (float)gv_num (pos, 1, 0), (float)gv_num (pos, 2, 0) };
				m->normals[vi] = (vec3_t){ (float)gv_num (nrm, 0, 0), (float)gv_num (nrm, 1, 1), (float)gv_num (nrm, 2, 0) };
				m->texcoords[vi] = (vec2_t){ (float)gv_num (uv, 0, 0), (float)gv_num (uv, 1, 0) };
				if (gv_has (col))
				{
					const u32 c = (u32)gv_num (col, 0, 0xffffffff);
					m->colors[0][vi] = (color4_t){ (c >> 24 & 255) / 255.0f, (c >> 16 & 255) / 255.0f, (c >> 8 & 255) / 255.0f,
						(c & 255) / 255.0f };
					have_color = true;
				}
				else
					m->colors[0][vi] = (color4_t){ 1, 1, 1, 1 };
			}
			if (!have_color)
			{
				free (m->colors[0]);
				m->colors[0] = 0;
				m->num_colors[0] = 0;
			}
			m->num_vertices = count * 3;
			for (size_t k = 0; k < count * 3; k++)
			{
				const gv_t *e = idx->items + first * 3 + k;
				if (e->kind == GV_OBJ && e->n)
					e = e->items; // {"Int16": n} / {"Int32": n}
				size_t vi = (size_t)gv_num (e, 0, 0);
				if (vi >= verts->n)
					vi = 0;
				vertex_t *vt = m->vertices + k;
				vt->position_idx = vt->normal_idx = vt->texcoord_idx = (int)vi;
				vt->tangent_idx = vt->matrix_idx = -1;
				vt->color_idx[0] = m->colors[0] ? (int)vi : -1;
				vt->color_idx[1] = -1;
				for (int ex = 0; ex < 7; ex++)
					vt->extra_texcoord_idx[ex] = -1;
			}
			m->material_idx = -1;
			// material
			if (binds && binds->kind == GV_ARR && matidx >= 0 && (size_t)matidx < binds->n)
			{
				const gv_t *mat = gv_get (binds->items + matidx, "Material");
				const gv_t *mn = gv_get (mat, "Name");
				const char *tex = material_texture (mat);
				material_t *gm = realloc (model->materials, (model->num_materials + 1) * sizeof (*gm));
				if (gm)
				{
					model->materials = gm;
					material_t *mt = gm + model->num_materials;
					memset (mt, 0, sizeof (*mt));
					snprintf (mt->name, sizeof (mt->name), "%s", mn && mn->kind == GV_STR ? mn->str : "material");
					mt->diffuse[0] = mt->diffuse[1] = mt->diffuse[2] = mt->diffuse[3] = 1.0f;
					if (tex)
					{
						char stem[64], png[PATH_MAX];
						stem_of (tex, stem, sizeof (stem));
						snprintf (png, sizeof (png), "%s/%s.png", dir, stem);
						FILE *f = fopen (png, "rb");
						if (f)
						{
							fclose (f);
							snprintf (mt->textures[0], sizeof (mt->textures[0]), "%s.png", stem);
							mt->num_textures = 1;
							mt->wrap_s[0] = mt->wrap_t[0] = 1;
							mt->min_filter[0] = mt->mag_filter[0] = 1;
						}
					}
					m->material_idx = (int)model->num_materials++;
				}
			}
		}
	}
	if (!model->num_meshes)
		goto done;
	err = ExportModelToGLB (model, out_path) == 0 ? ERR_OK : ERR_CANT_CREATE;
done:
	if (model)
		FreeModel (model);
	gv_free (&root);
	g_close (&g);
	return err;
}
