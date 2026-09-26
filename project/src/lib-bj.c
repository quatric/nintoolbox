// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "Bj" engine textures and mesh trees; see lib-bj.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-bj.h"
#include <string.h>
#include <math.h>

#define BJ_TX_MAGIC 0xfacc00ffu
#define BJ_MTM_MAGIC 0x80178e55u
#define BJ_MAX_NODES 4096
#define BJ_MAX_MESHES 65536

static u32 bj_rd32 (const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}
static u32 bj_rd16 (const u8 *p)
{
	return p[0] | p[1] << 8;
}
static float bj_rdf (const u8 *p)
{
	const u32 u = bj_rd32 (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

//-----------------------------------------------------------------------------
// textures

bool IsBjTx1 (const u8 *d, size_t size)
{
	return size >= 0x48 && bj_rd32 (d) == BJ_TX_MAGIC;
}

// Offset of texture IDX's record, or 0.
static u32 bj_tex_record (const u8 *d, size_t size, uint idx)
{
	if (!IsBjTx1 (d, size))
		return 0;
	u32 off = bj_rd32 (d + 8);
	for (uint i = 0; off && off + 0x24 <= size && i < 4096; i++)
	{
		if (i == idx)
			return off;
		off = bj_rd32 (d + off) & 0xffffff;
	}
	return 0;
}

uint BjTx1Count (const u8 *d, size_t size)
{
	uint n = 0;
	while (bj_tex_record (d, size, n))
		n++;
	return n;
}

enumError DecodeBjTexture (u8 **rgba, uint *width, uint *height, const u8 *d, size_t size1,
	const u8 *t2, size_t size2, uint idx)
{
	const u32 off = bj_tex_record (d, size1, idx);
	if (!off)
		return ERR_INVALID_DATA;
	const u32 loc = bj_rd32 (d + off + 12);
	const uint fmt = d[off + 16], frames = d[off + 17] ? d[off + 17] : 1;
	const uint flags = bj_rd16 (d + off + 20), w = bj_rd16 (d + off + 24),
			   h = bj_rd16 (d + off + 26);
	const u32 npal = bj_rd32 (d + off + 28);
	if (fmt != 12 || !w || !h || w > 4096 || h > 4096 || npal > 256
		|| off + 0x20 + (size_t)npal * 4 > size1)
		return ERR_INVALID_DATA;
	const bool mips = (flags & 0xc0) == 0x80 && w == h;
	size_t fsz = (size_t)w * h;
	if (mips)
	{
		fsz = 0;
		for (uint s = w; s; s >>= 1)
			fsz += (size_t)s * s;
	}
	(void)frames;
	(void)fsz;
	const size_t start = (size_t)(loc & 0xffff) * 2048;
	if (start + (size_t)w * h > size2)
		return ERR_INVALID_DATA;
	u8 *out = MALLOC ((size_t)w * h * 4);
	if (!out)
		return ERR_OUT_OF_MEMORY;
	const u8 *pal = d + off + 0x20;
	for (size_t i = 0; i < (size_t)w * h; i++)
	{
		const uint c = t2[start + i];
		if (c < npal)
			memcpy (out + 4 * i, pal + 4 * c, 4);
		else
			memset (out + 4 * i, 0, 4);
		if (!(flags & 0x1000))
			out[4 * i + 3] = 255;
	}
	*rgba = out;
	*width = w;
	*height = h;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// meshes

bool IsBjMtm (const u8 *d, size_t size)
{
	return size >= 0x100 && bj_rd32 (d) == BJ_MTM_MAGIC;
}

typedef struct
{
	const u8 *d;
	size_t size;
	model_t *model;
	BjTexFunc texname;
	void *ctx;
	uint n_nodes;
	uint n_visited;
	u32 *tex_ids;
	uint n_tex_ids;
} bj_ctx_t;

static bool bj_ok (const bj_ctx_t *c, u64 off, u64 len)
{
	return off + len <= c->size;
}

static void bj_matmul (float *r, const float *a, const float *b)
{
	float t[16];
	for (uint i = 0; i < 4; i++)
		for (uint j = 0; j < 4; j++)
			t[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j]
				+ a[i * 4 + 3] * b[12 + j];
	memcpy (r, t, sizeof (t));
}

static int bj_material (bj_ctx_t *c, u32 tex)
{
	model_t *m = c->model;
	for (uint i = 0; i < c->n_tex_ids; i++)
		if (c->tex_ids[i] == tex)
			return (int)i;
	material_t *nm = REALLOC (m->materials, (m->num_materials + 1) * sizeof (*nm));
	u32 *nt = REALLOC (c->tex_ids, (c->n_tex_ids + 1) * sizeof (*nt));
	if (nm)
		m->materials = nm;
	if (nt)
		c->tex_ids = nt;
	if (!nm || !nt)
		return -1;
	material_t *mt = m->materials + m->num_materials;
	memset (mt, 0, sizeof (*mt));
	mt->diffuse[0] = mt->diffuse[1] = mt->diffuse[2] = mt->diffuse[3] = 1.0f;
	ccp tn = tex != 0xffffffffu && c->texname ? c->texname (c->ctx, tex) : 0;
	if (tn)
	{
		snprintf (mt->name, sizeof (mt->name), "tex_%u", tex);
		snprintf (mt->textures[0], sizeof (mt->textures[0]), "%s", tn);
		mt->num_textures = 1;
		mt->wrap_s[0] = mt->wrap_t[0] = 1;
		mt->min_filter[0] = mt->mag_filter[0] = 1;
		mt->has_alpha = 1;
	}
	else
		snprintf (mt->name, sizeof (mt->name), "untextured");
	c->tex_ids[c->n_tex_ids] = tex;
	m->num_materials++;
	return (int)c->n_tex_ids++;
}

// Vertex record layout for the low four format bits: size, UV offset (or -1).
static void bj_vfmt (uint fl, uint *size, int *uv)
{
	static const u8 kind[16] = { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 0, 3, 0, 1, 0, 3 };
	const uint k = kind[fl & 15];
	*size = k & 1 ? 12 : 4;
	*uv = k & 1 ? 4 : -1;
}

static bool bj_add_mesh (bj_ctx_t *c, u32 mo, const float *W, uint node_idx, uint mesh_idx)
{
	const u8 *d = c->d;
	if (!bj_ok (c, mo, 192))
		return true;
	const u32 fl = bj_rd32 (d + mo + 8), tex = bj_rd32 (d + mo + 64);
	const u32 npos = bj_rd32 (d + mo + 76), nvert = bj_rd32 (d + mo + 88),
			  nidx = bj_rd32 (d + mo + 92), nprim = bj_rd32 (d + mo + 104);
	const u32 ppos = bj_rd32 (d + mo + 116), pvert = bj_rd32 (d + mo + 128),
			  pidx = bj_rd32 (d + mo + 132), pprim = bj_rd32 (d + mo + 144);
	uint vsz;
	int uvoff;
	bj_vfmt (fl, &vsz, &uvoff);
	if (!npos || !nvert || !nprim || npos > 0x100000 || nvert > 0x100000 || nidx > 0x1000000
		|| nprim > 0x10000 || !bj_ok (c, ppos, (u64)npos * 16)
		|| !bj_ok (c, pvert, (u64)nvert * vsz) || !bj_ok (c, pidx, (u64)nidx * 2)
		|| !bj_ok (c, pprim, (u64)nprim * 12))
		return true;

	// triangle soup as vertex-record indices
	size_t cap = 0, num = 0;
	u32 *soup = 0;
	for (uint p = 0; p < nprim; p++)
	{
		const u8 *pr = d + pprim + 12 * p;
		const uint pf = bj_rd16 (pr), cnt = bj_rd16 (pr + 8), st = bj_rd16 (pr + 10), ty = pf & 7;
		if (ty < 4 || ty > 6 || cnt < 3)
			continue;
		if ((pf & 8) && st + cnt > nidx)
			continue;
		if (!(pf & 8) && st + cnt > nvert)
			continue;
		for (uint t = 0; t + 2 < cnt; t++)
		{
			uint s[3];
			if (ty == 4)
			{
				if (t % 3)
					continue;
				s[0] = t, s[1] = t + 2, s[2] = t + 1;
			}
			else if (ty == 5)
			{
				s[0] = t, s[1] = (t & 1) ? t + 2 : t + 1, s[2] = (t & 1) ? t + 1 : t + 2;
			}
			else
				s[0] = 0, s[1] = t + 1, s[2] = t + 2;
			u32 v[3];
			bool ok = true;
			for (uint k = 0; k < 3; k++)
			{
				v[k] = pf & 8 ? bj_rd16 (d + pidx + 2 * (st + s[k])) : st + s[k];
				ok = ok && v[k] < nvert;
			}
			if (!ok)
				continue;
			u32 pi[3];
			for (uint k = 0; k < 3; k++)
				pi[k] = bj_rd16 (d + pvert + (size_t)v[k] * vsz);
			if (pi[0] == pi[1] || pi[1] == pi[2] || pi[0] == pi[2] || pi[0] >= npos || pi[1] >= npos
				|| pi[2] >= npos)
				continue;
			if (num + 3 > cap)
			{
				cap = cap ? cap * 2 : 768;
				u32 *ns = REALLOC (soup, cap * sizeof (*ns));
				if (!ns)
				{
					FREE (soup);
					return false;
				}
				soup = ns;
			}
			soup[num++] = v[0];
			soup[num++] = v[1];
			soup[num++] = v[2];
		}
	}
	if (!num)
	{
		FREE (soup);
		return true;
	}

	model_t *model = c->model;
	mesh_t *nm = REALLOC (model->meshes, (model->num_meshes + 1) * sizeof (*nm));
	if (!nm)
	{
		FREE (soup);
		return false;
	}
	model->meshes = nm;
	mesh_t *mesh = model->meshes + model->num_meshes++;
	memset (mesh, 0, sizeof (*mesh));
	snprintf (mesh->name, sizeof (mesh->name), "node%u_mesh%u", node_idx, mesh_idx);
	mesh->material_idx = bj_material (c, tex);
	mesh->vertices = CALLOC (num, sizeof (*mesh->vertices));
	mesh->positions = CALLOC (npos, sizeof (*mesh->positions));
	mesh->texcoords = uvoff >= 0 ? CALLOC (num, sizeof (*mesh->texcoords)) : 0;
	int *pmap = MALLOC (npos * sizeof (int));
	if (!mesh->vertices || !mesh->positions || !pmap || (uvoff >= 0 && !mesh->texcoords))
	{
		FREE (soup);
		FREE (pmap);
		return false;
	}
	memset (pmap, -1, npos * sizeof (int));
	for (size_t i = 0; i < num; i++)
	{
		const u8 *vr = d + pvert + (size_t)soup[i] * vsz;
		const uint pi = bj_rd16 (vr);
		if (pmap[pi] < 0)
		{
			const u8 *pp = d + ppos + 16 * pi;
			const float x = bj_rdf (pp), y = bj_rdf (pp + 4), z = bj_rdf (pp + 8);
			pmap[pi] = (int)mesh->num_positions;
			vec3_t *o = mesh->positions + mesh->num_positions++;
			o->x = x * W[0] + y * W[4] + z * W[8] + W[12];
			o->y = x * W[1] + y * W[5] + z * W[9] + W[13];
			o->z = x * W[2] + y * W[6] + z * W[10] + W[14];
		}
		vertex_t *v = mesh->vertices + i;
		v->position_idx = pmap[pi];
		v->normal_idx = v->tangent_idx = v->matrix_idx = -1;
		v->color_idx[0] = v->color_idx[1] = -1;
		v->texcoord_idx = -1;
		for (int e = 0; e < 7; e++)
			v->extra_texcoord_idx[e] = -1;
		if (uvoff >= 0)
		{
			mesh->texcoords[mesh->num_texcoords].u = bj_rdf (vr + uvoff);
			mesh->texcoords[mesh->num_texcoords].v = bj_rdf (vr + uvoff + 4);
			v->texcoord_idx = (int)mesh->num_texcoords++;
		}
	}
	mesh->num_vertices = num;
	FREE (pmap);
	FREE (soup);
	return true;
}

static bool bj_walk (bj_ctx_t *c, u32 off, const float *parent_world, uint depth)
{
	const u8 *d = c->d;
	while (off)
	{
		if (depth > 64 || c->n_visited++ > BJ_MAX_NODES || !bj_ok (c, off, 0xb4))
			return true;
		float W[16], M[16];
		for (uint i = 0; i < 16; i++)
			M[i] = bj_rdf (d + off + 112 + 4 * i);
		if (parent_world)
			bj_matmul (W, M, parent_world);
		else
			memcpy (W, M, sizeof (W));
		const uint nm = bj_rd32 (d + off + 108);
		const u32 mp = bj_rd32 (d + off + 176);
		if (nm && nm < 4096 && bj_ok (c, mp, (u64)nm * 4))
			for (uint i = 0; i < nm; i++)
				if (!bj_add_mesh (c, bj_rd32 (d + mp + 4 * i), W, c->n_visited, i))
					return false;
		const u32 child = bj_rd32 (d + off + 4), sib = bj_rd32 (d + off + 8);
		if (child && !bj_walk (c, child, W, depth + 1))
			return false;
		off = sib;
	}
	return true;
}

model_t *ParseBjMtm (const u8 *d, size_t size, BjTexFunc texname, void *ctx)
{
	if (!IsBjMtm (d, size))
		return 0;
	model_t *model = CALLOC (1, sizeof (*model));
	if (!model)
		return 0;
	bj_ctx_t c = { .d = d, .size = size, .model = model, .texname = texname, .ctx = ctx };
	const bool ok = bj_walk (&c, bj_rd32 (d + 20), 0, 0);
	FREE (c.tex_ids);
	if (!ok || !model->num_meshes)
	{
		FreeModel (model);
		return 0;
	}
	return model;
}

//-----------------------------------------------------------------------------
// audio

static u32 bj_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
static void bj_put32 (u8 *p, u32 v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

// 16-bit PCM WAV from planar signed 8-bit channels.
static enumError bj_wav (
	u8 **wav, size_t *wav_size, const u8 *const *ch, uint n_ch, size_t frames, u32 rate)
{
	const size_t bytes = frames * n_ch * 2;
	u8 *w = MALLOC (44 + bytes);
	if (!w)
		return ERR_OUT_OF_MEMORY;
	memcpy (w, "RIFF", 4);
	bj_put32 (w + 4, 36 + bytes);
	memcpy (w + 8, "WAVEfmt ", 8);
	bj_put32 (w + 16, 16);
	w[20] = 1, w[21] = 0, w[22] = n_ch, w[23] = 0;
	bj_put32 (w + 24, rate);
	bj_put32 (w + 28, rate * n_ch * 2);
	w[32] = n_ch * 2, w[33] = 0, w[34] = 16, w[35] = 0;
	memcpy (w + 36, "data", 4);
	bj_put32 (w + 40, bytes);
	u8 *o = w + 44;
	for (size_t i = 0; i < frames; i++)
		for (uint c = 0; c < n_ch; c++)
		{
			*o++ = 0;
			*o++ = ch[c][i];
		}
	*wav = w;
	*wav_size = 44 + bytes;
	return ERR_OK;
}

bool IsBjBsi (const u8 *d, size_t size)
{
	if (size < 0x30 || bj_be32 (d) != 0x0005002d)
		return false;
	const u32 n = bj_be32 (d + 4), tab = bj_be32 (d + 8);
	return n && n < 4096 && tab == 0x10 && tab + (u64)n * 32 <= size;
}

uint BjBsiCount (const u8 *d, size_t size)
{
	return IsBjBsi (d, size) ? bj_be32 (d + 4) : 0;
}

enumError DecodeBjBsiSample (u8 **wav, size_t *wav_size, const u8 *d, size_t size, uint idx)
{
	if (idx >= BjBsiCount (d, size))
		return ERR_NOTHING_TO_DO;
	const u8 *e = d + 0x10 + 32 * idx;
	const u32 rate = bj_be32 (e + 8), len = bj_be32 (e + 12), off = bj_be32 (e + 20);
	if (!rate || !len || rate > 192000 || (u64)off + len > size)
		return ERR_NOTHING_TO_DO;
	const u8 *ch = d + off;
	return bj_wav (wav, wav_size, &ch, 1, len, rate);
}

enumError DecodeBjMusic (u8 **wav, size_t *wav_size, const u8 *d, size_t size)
{
	if (size < 0x1000)
		return ERR_NOTHING_TO_DO;
	const size_t half = size / 2;
	const u8 *ch[2] = { d, d + half };
	return bj_wav (wav, wav_size, ch, 2, half, 32000);
}
