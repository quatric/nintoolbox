// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Rainbow Studios engine assets (see lib-rainbow.h).
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-rainbow.h"
#include "lib-nintendo.h"
#include "lib-excite.h"

#define GCT_FMT_CMPR 41
#define GCT_FMT_CI8 58
#define GCT_MAX_SURFACES 16

typedef struct
{
	uint fmt, n, w, h, best_w, best_h, best_size;
	const u8 *pal, *best;
} gct_t;

static bool gct_parse (gct_t *g, const u8 *d, uint size)
{
	if (!d || size < 0x28 || rd_be32 (d) != 2)
		return false;
	g->fmt = rd_be32 (d + 4);
	u64 o = 12;
	g->pal = 0;
	if (g->fmt == GCT_FMT_CI8)
	{
		if (rd_be32 (d + 8) != 256)
			return false;
		g->pal = d + o;
		o += 512;
	}
	else if (g->fmt != GCT_FMT_CMPR)
		return false;
	if (o + 12 > size)
		return false;
	g->n = rd_be32 (d + o);
	g->w = rd_be32 (d + o + 4);
	g->h = rd_be32 (d + o + 8);
	o += 12;
	if (!g->n || g->n > GCT_MAX_SURFACES || !g->w || !g->h || g->w > 4096 || g->h > 4096)
		return false;
	for (uint i = 0; i < g->n; i++)
	{
		if (o + 12 > size)
			return false;
		const u32 w = rd_be32 (d + o), h = rd_be32 (d + o + 4), sz = rd_be32 (d + o + 8);
		o += 12;
		if (!w || !h || w > 4096 || h > 4096 || o + sz > size)
			return false;
		g->best_w = w;
		g->best_h = h;
		g->best_size = sz;
		g->best = d + o;
		o += sz;
	}
	return g->best_w == g->w && g->best_h == g->h;
}

bool IsGCT (const u8 *data, uint size)
{
	gct_t g;
	return gct_parse (&g, data, size);
}

enumError DecodeGCT (u8 **rgba, uint *width, uint *height, const u8 *data, uint size)
{
	gct_t g;
	if (!gct_parse (&g, data, size))
		return ERR_NOTHING_TO_DO;
	const enumError err = g.fmt == GCT_FMT_CMPR
		? DecodeGXTexture_RGBA (rgba, g.w, g.h, 14, g.best, g.best_size, 0, 0, 0)
		: DecodeGXTexture_RGBA (rgba, g.w, g.h, 9, g.best, g.best_size, g.pal, 256, 2);
	if (!err)
	{
		*width = g.w;
		*height = g.h;
	}
	return err;
}

//-----------------------------------------------------------------------------
///////////////			.gcg geometry			///////////////
//-----------------------------------------------------------------------------

#include <math.h>
#include <string.h>

#define GCG_MAX_MATERIALS 64
#define GCG_MAX_STRIPS 256

static inline uint gcg_be16 (const u8 *p)
{
	return (uint)p[0] << 8 | p[1];
}

typedef struct
{
	u8 flags;
	u8 pos_vcd, pos_type, pos_frac;
	u8 clr_vcd;
	u8 tex_vcd, tex_type, tex_frac;
	uint npos, spos, nclr, sclr, ntex, stex, dl_size;
	const u8 *pos, *clr, *tex, *dl;
	uint mat;
} gcg_strip_t;

static bool gcg_read_strip (const u8 *d, uint size, u64 *po, gcg_strip_t *s)
{
	u64 o = *po;
	if (o + 8 > size)
		return false;
	memset (s, 0, sizeof (*s));
	s->flags = d[o++];
	s->pos_vcd = d[o];
	s->pos_type = d[o + 1];
	s->pos_frac = d[o + 2];
	o += 3;
	const bool has_clr = (s->flags & 8) || !(s->flags & 1);
	if (has_clr)
	{
		s->clr_vcd = d[o];
		o += 2;
	}
	const bool has_tex = (s->flags & 6) != 0;
	if (has_tex)
	{
		s->tex_vcd = d[o];
		s->tex_type = d[o + 1];
		s->tex_frac = d[o + 2];
		o += 3;
	}
	if (o + 4 > size)
		return false;
	s->npos = gcg_be16 (d + o);
	s->spos = d[o + 2];
	o += 3;
	if (has_clr)
	{
		s->nclr = gcg_be16 (d + o);
		s->sclr = d[o + 2];
		o += 3;
	}
	if (has_tex)
	{
		s->ntex = gcg_be16 (d + o);
		s->stex = d[o + 2];
		o += 3;
	}
	if (o + 4 > size)
		return false;
	s->dl_size = rd_be32 (d + o);
	o += 4;
	const u64 total
		= (u64)s->npos * s->spos + (u64)s->nclr * s->sclr + (u64)s->ntex * s->stex + s->dl_size;
	if (o + total > size)
		return false;
	s->pos = d + o;
	o += (u64)s->npos * s->spos;
	s->clr = d + o;
	o += (u64)s->nclr * s->sclr;
	s->tex = d + o;
	o += (u64)s->ntex * s->stex;
	s->dl = d + o;
	o += s->dl_size;
	*po = o;
	return true;
}

// Parse the header; returns the offset of the first strip block (0 = invalid).
static u64 gcg_header (const u8 *d, uint size, uint *nmat, uint *nstrips)
{
	if (!d || size < 0xf0 + 12 || memcmp (d, "gcg", 4) || rd_be32 (d + 4) != 5
		|| rd_be32 (d + 8) != 1)
		return 0;
	*nmat = rd_be32 (d + 0xec);
	if (*nmat > GCG_MAX_MATERIALS)
		return 0;
	const u64 o = 0xf0 + (u64)*nmat * 0x40;
	if (o + 12 > size)
		return 0;
	*nstrips = rd_be32 (d + o + 8);
	if (*nstrips > GCG_MAX_STRIPS)
		return 0;
	return o + 12;
}

bool IsRainbowGCG (const u8 *d, uint size)
{
	uint nmat, ns;
	u64 o = gcg_header (d, size, &nmat, &ns);
	if (!o)
		return false;
	for (uint i = 0; i < ns; i++)
	{
		gcg_strip_t s;
		o += 4;
		if (o >= size || !gcg_read_strip (d, size, &o, &s))
			return false;
	}
	return o == size;
}

static float gcg_comp (const u8 *p, uint type, uint frac)
{
	float v;
	switch (type)
	{
		case 0:
			v = p[0];
			break;
		case 1:
			v = (int8_t)p[0];
			break;
		case 2:
			v = (float)gcg_be16 (p);
			break;
		case 3:
			v = (float)(int16_t)gcg_be16 (p);
			break;
		default:
		{
			const u32 u = rd_be32 (p);
			memcpy (&v, &u, 4);
			return v;
		}
	}
	return v / (float)(1u << frac);
}

static uint gcg_type_size (uint type)
{
	return type <= 1 ? 1 : type <= 3 ? 2 : 4;
}

typedef struct
{
	uint v[3]; // position, colour, uv indices per vertex
} gcg_vtx_t;

static bool gcg_add_strip (model_t *model, const gcg_strip_t *s, uint mat, ccp name, uint idx)
{
	if ((s->pos_vcd != 2 && s->pos_vcd != 3) || !s->npos || !s->spos)
		return true;
	// bit0 = the colour comes from a fixed table (index8, no array in the file)
	const bool has_clr = (s->flags & 8) || !(s->flags & 1);
	const bool has_tex = (s->flags & 6) != 0;
	const uint w_pos = s->pos_vcd == 2 ? 1 : 2;
	const uint w_clr = has_clr ? (s->clr_vcd == 2 ? 1 : s->clr_vcd == 3 ? 2 : 0) : 1;
	const uint w_tex = has_tex ? (s->tex_vcd == 2 ? 1 : s->tex_vcd == 3 ? 2 : 0) : 0;
	if ((has_clr && !w_clr) || (has_tex && !w_tex))
		return true;
	const uint n_tex_sets = has_tex ? ((s->flags & 2) && (s->flags & 4) ? 2 : 1) : 0;
	const uint stride = w_pos + w_clr + w_tex * n_tex_sets;

	// expand the display list to a triangle soup of (pos, tex) index pairs
	size_t cap = 0, num = 0;
	gcg_vtx_t *soup = 0;
	const u8 *q = s->dl, *end = s->dl + s->dl_size;
	uint max_p = 0, max_t = 0;
	while (q < end)
	{
		const uint op = *q;
		if (!op)
		{
			q++;
			continue;
		}
		if ((op & 0xf8) != 0x98 && (op & 0xf8) != 0x90 && (op & 0xf8) != 0xa0)
			break;
		if (q + 3 > end)
			break;
		const uint n = gcg_be16 (q + 1);
		q += 3;
		if (q + (u64)n * stride > end)
			break;
		gcg_vtx_t *v = MALLOC ((n ? n : 1) * sizeof (*v));
		if (!v)
		{
			FREE (soup);
			return false;
		}
		bool ok = true;
		for (uint i = 0; i < n && ok; i++)
		{
			const u8 *r = q + (u64)i * stride;
			v[i].v[0] = w_pos == 1 ? r[0] : gcg_be16 (r);
			r += w_pos;
			v[i].v[1] = w_clr ? (w_clr == 1 ? r[0] : gcg_be16 (r)) : 0;
			r += w_clr;
			v[i].v[2] = w_tex ? (w_tex == 1 ? r[0] : gcg_be16 (r)) : 0;
			ok = v[i].v[0] < s->npos && (!has_tex || v[i].v[2] < s->ntex);
			if (ok)
			{
				if (v[i].v[0] > max_p)
					max_p = v[i].v[0];
				if (v[i].v[2] > max_t)
					max_t = v[i].v[2];
			}
		}
		q += (u64)n * stride;
		if (!ok)
		{
			FREE (v);
			FREE (soup);
			return true;
		}
		// triangles, front faces counter-clockwise for glTF
		for (uint i = 0; i + 2 < n; i++)
		{
			gcg_vtx_t t[3];
			if ((op & 0xf8) == 0x98)
			{
				t[0] = v[i + 2];
				t[1] = (i & 1) ? v[i] : v[i + 1];
				t[2] = (i & 1) ? v[i + 1] : v[i];
			}
			else if ((op & 0xf8) == 0x90)
			{
				if (i % 3)
					continue;
				t[0] = v[i];
				t[1] = v[i + 1];
				t[2] = v[i + 2];
			}
			else
			{
				t[0] = v[i + 1];
				t[1] = v[i + 2];
				t[2] = v[0];
			}
			if (num + 3 > cap)
			{
				cap = cap ? cap * 2 : 768;
				gcg_vtx_t *ns = REALLOC (soup, cap * sizeof (*ns));
				if (!ns)
				{
					FREE (v);
					FREE (soup);
					return false;
				}
				soup = ns;
			}
			soup[num++] = t[0];
			soup[num++] = t[1];
			soup[num++] = t[2];
		}
		FREE (v);
	}
	if (!num)
	{
		FREE (soup);
		return true;
	}

	mesh_t *nm = REALLOC (model->meshes, (model->num_meshes + 1) * sizeof (*nm));
	if (!nm)
	{
		FREE (soup);
		return false;
	}
	model->meshes = nm;
	mesh_t *mesh = model->meshes + model->num_meshes;
	memset (mesh, 0, sizeof (*mesh));
	snprintf (mesh->name, sizeof (mesh->name), "%s_%u", name, idx);
	mesh->material_idx = mat < model->num_materials ? (int)mat : 0;
	mesh->num_positions = max_p + 1;
	mesh->positions = CALLOC (mesh->num_positions, sizeof (*mesh->positions));
	mesh->vertices = CALLOC (num, sizeof (*mesh->vertices));
	if (has_tex)
	{
		mesh->num_texcoords = max_t + 1;
		mesh->texcoords = CALLOC (mesh->num_texcoords, sizeof (*mesh->texcoords));
	}
	if (!mesh->positions || !mesh->vertices || (has_tex && !mesh->texcoords))
	{
		FREE (soup);
		return false;
	}
	model->num_meshes++;

	const uint psz = gcg_type_size (s->pos_type), pn = s->spos / psz;
	for (size_t i = 0; i < mesh->num_positions; i++)
	{
		const u8 *p = s->pos + i * s->spos;
		mesh->positions[i].x = pn > 0 ? gcg_comp (p, s->pos_type, s->pos_frac) : 0;
		mesh->positions[i].y = pn > 1 ? gcg_comp (p + psz, s->pos_type, s->pos_frac) : 0;
		mesh->positions[i].z = pn > 2 ? gcg_comp (p + 2 * psz, s->pos_type, s->pos_frac) : 0;
	}
	const uint tsz = gcg_type_size (s->tex_type);
	for (size_t i = 0; has_tex && i < mesh->num_texcoords; i++)
	{
		const u8 *p = s->tex + i * s->stex;
		mesh->texcoords[i].u = gcg_comp (p, s->tex_type, s->tex_frac);
		mesh->texcoords[i].v
			= s->stex >= 2 * tsz ? gcg_comp (p + tsz, s->tex_type, s->tex_frac) : 0;
	}
	mesh->num_vertices = num;
	for (size_t i = 0; i < num; i++)
	{
		vertex_t *v = mesh->vertices + i;
		v->position_idx = (int)soup[i].v[0];
		v->normal_idx = -1;
		v->tangent_idx = -1;
		v->texcoord_idx = has_tex ? (int)soup[i].v[2] : -1;
		v->matrix_idx = -1;
		v->color_idx[0] = v->color_idx[1] = -1;
		for (int e = 0; e < 7; e++)
			v->extra_texcoord_idx[e] = -1;
	}
	FREE (soup);
	return true;
}

model_t *ParseRainbowGCG (const u8 *d, uint size, RainbowTexFunc texname, void *ctx)
{
	uint nmat, ns;
	u64 o = gcg_header (d, size, &nmat, &ns);
	if (!o || !IsRainbowGCG (d, size))
		return 0;
	model_t *model = CALLOC (1, sizeof (*model));
	if (!model)
		return 0;
	model->num_materials = nmat ? nmat : 1;
	model->materials = CALLOC (model->num_materials, sizeof (*model->materials));
	if (!model->materials)
	{
		FREE (model);
		return 0;
	}
	for (uint i = 0; i < model->num_materials; i++)
	{
		material_t *m = model->materials + i;
		char nm[0x41];
		memset (nm, 0, sizeof (nm));
		if (nmat)
			memcpy (nm, d + 0xf0 + (u64)i * 0x40, 0x40);
		snprintf (m->name, sizeof (m->name), "%s", nm[0] ? nm : "Material");
		m->diffuse[0] = m->diffuse[1] = m->diffuse[2] = m->diffuse[3] = 1.0f;
		ccp tn = nm[0] && texname ? texname (ctx, nm) : 0;
		if (tn)
		{
			snprintf (m->textures[0], sizeof (m->textures[0]), "%s", tn);
			m->num_textures = 1;
			m->wrap_s[0] = m->wrap_t[0] = 1;
			m->min_filter[0] = m->mag_filter[0] = 1;
			m->has_alpha = 1;
		}
	}
	char name[0x81];
	memset (name, 0, sizeof (name));
	memcpy (name, d + 0x0c, 0x80);
	for (uint i = 0; i < ns; i++)
	{
		gcg_strip_t s;
		const uint mat = rd_be32 (d + o);
		o += 4;
		gcg_read_strip (d, size, &o, &s);
		if (!gcg_add_strip (model, &s, mat, name[0] ? name : "Geom", i))
		{
			FreeModel (model);
			return 0;
		}
	}
	if (!model->num_meshes)
	{
		FreeModel (model);
		return 0;
	}
	return model;
}
