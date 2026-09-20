// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Humongous / Cat Daddy Resource.rez; see lib-rez.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-rez.h"
#include "lib-excite.h"
#include "lib-dspadpcm.h"
#include <string.h>

#define REZ_MAX_UNPACKED (64u << 20)

static u32 rz_be32 (const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static u32 rz_be16 (const u8 *p) { return p[0] << 8 | p[1]; }

// Locate the slot table: false if the footer does not describe one.
static bool rz_slots (const u8 *d, size_t size, const u8 **slots, uint *n, uint *start)
{
	if (size < 4096)
		return false;
	const size_t fs = size - 2048;
	const size_t count = rz_be32 (d + fs + 8);
	const size_t tb = (count * 24 + 2047) / 2048 * 2048;
	if (!count || count > 0x100000 || tb > fs)
		return false;
	*slots = d + fs - tb;
	*n = count;
	*start = rz_be32 (d + fs + 4);
	return true;
}

bool IsHumongousRez (const u8 *d, size_t size)
{
	const u8 *slots;
	uint n, start;
	if (!rz_slots (d, size, &slots, &n, &start))
		return false;
	uint groups = 0;
	for (uint i = 0; i < n && groups < 4; i++)
	{
		const u8 *s = slots + (size_t)i * 24;
		const size_t off = rz_be32 (s), sz = rz_be32 (s + 4), hs = rz_be16 (s + 14);
		if (sz && hs >= 0x20 && hs <= sz && off + sz <= size - 2048 && rz_be32 (d + off + sz - hs) < 0x10000)
			groups++;
	}
	return groups > 0;
}

// Returns the unpacked size, or 0 on error.
static size_t rz_unpack (const u8 *d, size_t size, size_t p, u8 *out, size_t cap)
{
	if (p + 4 > size)
		return 0;
	const size_t n = rz_be32 (d + p);
	if (n > cap)
		return 0;
	p += 4;
	size_t o = 0;
	while (o < n)
	{
		if (p >= size)
			return 0;
		const uint b = d[p++], c = b & 0x7f;
		if (b & 0x80)
		{
			if (p + c > size || o + c > n)
				return 0;
			memcpy (out + o, d + p, c);
			p += c;
			o += c;
		}
		else
		{
			if (p + 2 > size)
				return 0;
			const size_t dist = rz_be16 (d + p);
			p += 2;
			if (dist > o || o + c > n)
				return 0;
			for (uint i = 0; i < c; i++, o++)
				out[o] = out[o - dist];
		}
	}
	return n;
}

static bool rz_add (rez_res_t **list, uint *count, uint *cap, const rez_res_t *e)
{
	if (*count == *cap)
	{
		rez_res_t *nl = REALLOC (*list, (*cap *= 2) * sizeof (**list));
		if (!nl)
			return false;
		*list = nl;
	}
	(*list)[(*count)++] = *e;
	return true;
}

// Slot record R (24 bytes) as a resource of a wanted kind.
static bool rz_res (const u8 *r, size_t size, uint g, uint i, rez_res_t *e)
{
	const int type = (s16)rz_be16 (r + 12);
	if (type != REZ_TEXTURE && type != REZ_SOUND && type != REZ_VIDEO && type != REZ_MESH && type != REZ_ANIM && type != REZ_MOTION)
		return false;
	e->group = g;
	e->index = i;
	e->kind = type;
	e->offset = rz_be32 (r);
	e->csize = rz_be32 (r + 4);
	e->usize = rz_be32 (r + 8);
	e->flags = rz_be16 (r + 14);
	return e->offset + (size_t)e->csize <= size && e->usize && (e->usize <= REZ_MAX_UNPACKED || type == REZ_VIDEO);
}

rez_res_t *ListRezResources (const u8 *d, size_t size, uint *count)
{
	*count = 0;
	const u8 *slots;
	uint n, start;
	if (!rz_slots (d, size, &slots, &n, &start))
		return 0;
	uint cap = 256;
	rez_res_t *list = MALLOC (cap * sizeof (*list));
	if (!list)
		return 0;
	for (uint g = 0; g < n; g++)
	{
		const u8 *s = slots + (size_t)g * 24;
		const size_t off = rz_be32 (s), sz = rz_be32 (s + 4), hs = rz_be16 (s + 14);
		if (!sz)
			continue;
		rez_res_t e;
		if (hs < 0x20)
		{
			// a resource on its own; only videos are recognisable without unpacking
			if (rz_res (s, size, g + start, 0, &e) && (e.kind != REZ_VIDEO || (e.usize > 0x40 && !(e.flags & 1) && !memcmp (d + e.offset, "THP", 4)))
				&& (e.kind != REZ_MOTION || (!(e.flags & 1) && e.csize > 0x40 && rz_be32 (d + e.offset + 12) == 12 + 8 * rz_be32 (d + e.offset + 4)
					&& 0x14 + (size_t)rz_be32 (d + e.offset) * rz_be32 (d + e.offset + 12) <= e.csize)))
				if (!rz_add (&list, count, &cap, &e))
					return list;
			continue;
		}
		if (hs > sz || off + sz > size - 2048)
			continue;
		const u8 *t = d + off + sz - hs;
		const size_t cnt = rz_be32 (t);
		if (!cnt || 16 + cnt * 24 > hs || rz_be32 (t + 16) < off || rz_be32 (t + 16) >= off + sz)
			continue;
		for (uint i = 0; i < cnt; i++)
			if (rz_res (t + 16 + i * 24, size, g + start, i, &e) && e.kind != REZ_VIDEO && e.kind != REZ_MOTION)
				if (!rz_add (&list, count, &cap, &e))
					return list;
	}
	return list;
}

u8 *LoadRezResource (const u8 *d, size_t size, const rez_res_t *r, size_t *out_size)
{
	const size_t cap = (size_t)r->usize + 64;
	u8 *buf = MALLOC (cap);
	if (!buf)
		return 0;
	size_t n;
	if (r->flags & 1)
		n = rz_unpack (d, size, r->offset, buf, cap);
	else
		memcpy (buf, d + r->offset, n = r->csize < r->usize ? r->csize : r->usize);
	if (!n)
	{
		FREE (buf);
		return 0;
	}
	*out_size = n;
	return buf;
}

enumError DecodeRezTexture (u8 **rgba, uint *width, uint *height, const u8 *buf, size_t n)
{
	if (n < 0x60)
		return ERR_INVALID_DATA;
	const uint w = rz_be16 (buf), h = rz_be16 (buf + 2), fmt = rz_be16 (buf + 6);
	size_t body;
	const u8 *pal = 0;
	uint pal_count = 0;
	enumError err = ERR_INVALID_DATA;
	if (!w || !h || w > 2048 || h > 2048)
		return err;
	*width = w;
	*height = h;
	if (fmt == 9 || fmt == 8)
	{
		pal_count = fmt == 9 ? 256 : 16;
		body = fmt == 9 ? (size_t)w * h : (size_t)w * h / 2;
		if (n < 0x80 + body + 2 * pal_count)
			return err;
		pal = buf + 0x80 + body;
		err = DecodeGXTexture_RGBA (rgba, w, h, fmt, buf + 0x80, (uint)body, pal, pal_count, 2);
	}
	else if (fmt == 5 || fmt == 6)
	{
		body = (size_t)w * h * (fmt == 6 ? 4 : 2);
		if (n < 0x60 + body)
			return err;
		err = DecodeGXTexture_RGBA (rgba, w, h, fmt, buf + 0x60, (uint)body, 0, 0, 0);
	}
	return err;
}

static void rz_put32 (u8 *p, u32 v) { p[0] = v, p[1] = v >> 8, p[2] = v >> 16, p[3] = v >> 24; }

enumError DecodeRezSound (u8 **wav, size_t *wav_size, const u8 *r, size_t n)
{
	if (n < 0x88 || rz_be32 (r) != 6)
		return ERR_INVALID_DATA;
	const u32 rate = rz_be32 (r + 4), bytes = rz_be32 (r + 12);
	if (!rate || rate > 192000 || !bytes || bytes > n - 0x80)
		return ERR_INVALID_DATA;
	const u32 samples = bytes / 8 * 14;
	s16 coefs[16];
	for (uint i = 0; i < 16; i++)
		coefs[i] = (s16)rz_be16 (r + 0x44 + 2 * i);
	const size_t out = 44 + (size_t)samples * 2;
	u8 *w = MALLOC (out);
	if (!w)
		return ERR_OUT_OF_MEMORY;
	memcpy (w, "RIFF", 4);
	rz_put32 (w + 4, out - 8);
	memcpy (w + 8, "WAVEfmt ", 8);
	rz_put32 (w + 16, 16);
	w[20] = 1, w[21] = 0, w[22] = 1, w[23] = 0;
	rz_put32 (w + 24, rate);
	rz_put32 (w + 28, rate * 2);
	w[32] = 2, w[33] = 0, w[34] = 16, w[35] = 0;
	memcpy (w + 36, "data", 4);
	rz_put32 (w + 40, samples * 2);
	int h1 = 0, h2 = 0;
	for (u32 f = 0, done = 0; done < samples; f++, done += 14)
	{
		s16 pcm[14];
		DspAdpcmDecodeBlock (r + 0x80 + (size_t)f * 8, 14, pcm, coefs, &h1, &h2);
		for (uint k = 0; k < 14; k++)
		{
			u8 *o = w + 44 + 2 * (size_t)(done + k);
			o[0] = pcm[k], o[1] = pcm[k] >> 8;
		}
	}
	*wav = w;
	*wav_size = out;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// geometry (type 75) and object animation (type 86)

#define REZ_MAX_NODES 4096

static float rz_f (const u8 *p)
{
	const u32 u = rz_be32 (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

static bool rz_ok (size_t size, size_t off, size_t len) { return off <= size && len <= size - off; }

typedef struct
{
	size_t base, dl, dlsz, pos, col, nrm, uv, bat;
	uint np, ncol, nn, nuv, nb, nch;
	float t[3], q[4], s[3], pv[3];
	uint depth;
	size_t weights, bones;	// per-position influences, first bone of the skeleton
	uint nbones;
} rz_node_t;

typedef struct
{
	const u8 *d;
	size_t size;
	rz_node_t *nodes;
	uint n;
	bool skinned;
} rz_tree_t;

// A bone (32 bytes {f32 offset[3], 0, u32 children, ...}) and its children.
static size_t rz_bones (const u8 *d, size_t size, size_t p, uint *n)
{
	if (++*n > 256 || !rz_ok (size, p, 32))
		return (size_t)-1;
	const uint nc = rz_be32 (d + p + 16);
	p += 32;
	for (uint i = 0; i < nc && p != (size_t)-1; i++)
		p = rz_bones (d, size, p, n);
	return p;
}

// One object at BASE and (depth first) its children; returns the end offset or ~0.
static size_t rz_walk (rz_tree_t *t, size_t base, uint depth)
{
	const u8 *d = t->d;
	if (t->n >= REZ_MAX_NODES || !rz_ok (t->size, base, 0xe0) || (rz_be32 (d + base) & 0x8003) != 3
		|| rz_be32 (d + base) > 0xffff)
		return (size_t)-1;
	rz_node_t *o = t->nodes + t->n++;
	memset (o, 0, sizeof (*o));
	o->base = base;
	o->depth = depth;
	o->dlsz = rz_be32 (d + base + 8);
	o->np = rz_be32 (d + base + 0x10);
	o->ncol = rz_be32 (d + base + 0x18);
	o->nn = rz_be32 (d + base + 0x20);
	o->nuv = rz_be32 (d + base + 0x28);
	o->nb = rz_be32 (d + base + 0x30);
	o->nch = rz_be32 (d + base + 0x38);
	if (o->np > 0x10000 || o->ncol > 0x10000 || o->nn > 0x10000 || o->nuv > 0x10000 || o->nb > 64 || o->nch > 256
		|| o->dlsz > t->size)
		return (size_t)-1;
	for (uint i = 0; i < 3; i++)
		o->t[i] = rz_f (d + base + 0x48 + 4 * i), o->s[i] = rz_f (d + base + 0x64 + 4 * i),
		o->pv[i] = rz_f (d + base + 0x70 + 4 * i);
	for (uint i = 0; i < 4; i++)
		o->q[i] = rz_f (d + base + 0x54 + 4 * i);
	size_t c = base + 0xe0;
	o->dl = c;
	c += o->dlsz;
	o->pos = c;
	c += (size_t)o->np * 12;
	o->col = c;
	c += (size_t)o->ncol * 4;
	o->nrm = c;
	c += (size_t)o->nn * 12;
	if (rz_be32 (d + base) & 0x100)
	{
		o->weights = c;
		c += (size_t)o->np * 16;
	}
	o->uv = c;
	c += (size_t)o->nuv * 8;
	o->bat = c;
	c += (size_t)o->nb * 20;
	if (!rz_ok (t->size, base, c - base))
		return (size_t)-1;
	if (rz_be32 (d + base + 0x44))
	{
		// skeleton {u32 bones, 12 bytes, 2 x bones u32}, then the bone tree
		if (!rz_ok (t->size, c, 16))
			return (size_t)-1;
		const size_t nb = rz_be32 (d + c);
		c += 16 + nb * 8;
		o->bones = c;
		if ((c = rz_bones (d, t->size, c, &o->nbones)) == (size_t)-1 || o->nbones != nb)
			return (size_t)-1;
	}
	c += 32 - ((c - base) & 31);
	for (uint i = 0; i < o->nch; i++)
		if ((c = rz_walk (t, c, depth + 1)) == (size_t)-1)
			return c;
	return c;
}

// Triangles of one display list slice; indices are appended as u32 tuples.
static size_t rz_dl (const rz_node_t *o, const u8 *d, size_t size, size_t dl, size_t dlsz, uint (**out)[4])
{
	const uint wp = o->np > 256 ? 2 : 1, wn = o->nn > 256 ? 2 : 1, wc = o->ncol > 256 ? 2 : 1, wu = o->nuv > 256 ? 2 : 1;
	const bool hn = o->nn, hc = o->ncol, hu = o->nuv;
	const uint vs = wp + (hn ? wn : 0) + (hc ? wc : 0) + (hu ? wu : 0);
	const u8 *p = d + dl, *end = d + dl + dlsz;
	size_t cap = 3072, cnt = 0;
	uint (*soup)[4] = MALLOC (cap * sizeof (*soup));
	if (!soup || !rz_ok (size, dl, dlsz))
		return 0;
	static uint idx[8192][4];
	while (p + 3 <= end && *p)
	{
		const uint cmd = *p & 0xf8;
		uint n = rz_be16 (p + 1);
		p += 3;
		if (n > 8192 || (size_t)(end - p) < (size_t)n * vs || (cmd != 0x80 && cmd != 0x90 && cmd != 0x98 && cmd != 0xa0))
			break;
		bool bad = false;
		for (uint i = 0; i < n; i++)
		{
			idx[i][0] = wp == 2 ? rz_be16 (p) : p[0];
			p += wp;
			idx[i][1] = idx[i][2] = idx[i][3] = 0;
			if (hn)
				idx[i][1] = wn == 2 ? rz_be16 (p) : p[0], p += wn;
			if (hc)
				idx[i][2] = wc == 2 ? rz_be16 (p) : p[0], p += wc;
			if (hu)
				idx[i][3] = wu == 2 ? rz_be16 (p) : p[0], p += wu;
			if (idx[i][0] >= o->np || idx[i][1] >= (hn ? o->nn : 1) || idx[i][2] >= (hc ? o->ncol : 1)
				|| idx[i][3] >= (hu ? o->nuv : 1))
				bad = true;
		}
		if (bad)
			break;
		#define REMIT(a, b, c) do { \
			if (cnt + 3 > cap) { cap *= 2; uint (*ns)[4] = REALLOC (soup, cap * sizeof (*soup)); if (!ns) { FREE (soup); return 0; } soup = ns; } \
			memcpy (soup[cnt++], idx[a], sizeof (idx[0])); memcpy (soup[cnt++], idx[b], sizeof (idx[0])); \
			memcpy (soup[cnt++], idx[c], sizeof (idx[0])); } while (0)
		if (cmd == 0x90)
			for (uint i = 0; i + 2 < n; i += 3)
				REMIT (i, i + 1, i + 2);
		else if (cmd == 0x80)
			for (uint i = 0; i + 3 < n; i += 4)
			{
				REMIT (i, i + 1, i + 2);
				REMIT (i, i + 2, i + 3);
			}
		else if (cmd == 0x98)
			for (uint i = 0; i + 2 < n; i++)
			{
				if (idx[i][0] == idx[i + 1][0] || idx[i + 1][0] == idx[i + 2][0] || idx[i][0] == idx[i + 2][0])
					continue;
				if (i & 1)
					REMIT (i + 1, i, i + 2);
				else
					REMIT (i, i + 1, i + 2);
			}
		else
			for (uint i = 1; i + 1 < n; i++)
				REMIT (0, i, i + 1);
		#undef REMIT
	}
	if (!cnt)
	{
		FREE (soup);
		return 0;
	}
	*out = soup;
	return cnt;
}

static bool rz_mesh (model_t *m, const rz_tree_t *t, uint k, uint batch, size_t dl, size_t dlsz, u32 tex_id,
	RezTexFunc texname, void *ctx)
{
	const rz_node_t *o = t->nodes + k;
	const u8 *d = t->d;
	uint (*soup)[4] = 0;
	const size_t cnt = rz_dl (o, d, t->size, dl, dlsz, &soup);
	if (!cnt)
		return true;
	mesh_t *nm = REALLOC (m->meshes, (m->num_meshes + 1) * sizeof (*nm));
	if (!nm)
	{
		FREE (soup);
		return false;
	}
	m->meshes = nm;
	mesh_t *mesh = m->meshes + m->num_meshes++;
	memset (mesh, 0, sizeof (*mesh));
	snprintf (mesh->name, sizeof (mesh->name), "obj%u_b%u", k, batch);
	mesh->vertices = CALLOC (cnt, sizeof (*mesh->vertices));
	mesh->positions = CALLOC (cnt, sizeof (*mesh->positions));
	if (o->nn)
		mesh->normals = CALLOC (cnt, sizeof (*mesh->normals));
	if (o->nuv)
		mesh->texcoords = CALLOC (cnt, sizeof (*mesh->texcoords));
	if (!mesh->vertices || !mesh->positions || (o->nn && !mesh->normals) || (o->nuv && !mesh->texcoords))
	{
		FREE (soup);
		return false;
	}
	for (size_t i = 0; i < cnt; i++)
	{
		const u8 *pp = d + o->pos + 12 * (size_t)soup[i][0];
		mesh->positions[i] = (vec3_t){ rz_f (pp), rz_f (pp + 4), rz_f (pp + 8) };
		vertex_t *v = mesh->vertices + i;
		v->position_idx = (int)i;
		v->normal_idx = v->tangent_idx = v->matrix_idx = v->texcoord_idx = -1;
		v->color_idx[0] = v->color_idx[1] = -1;
		for (int e = 0; e < 7; e++)
			v->extra_texcoord_idx[e] = -1;
		if (o->nn)
		{
			const u8 *np_ = d + o->nrm + 12 * (size_t)soup[i][1];
			mesh->normals[i] = (vec3_t){ rz_f (np_), rz_f (np_ + 4), rz_f (np_ + 8) };
			v->normal_idx = (int)i;
		}
		if (o->nuv)
		{
			const u8 *tp = d + o->uv + 8 * (size_t)soup[i][3];
			mesh->texcoords[i] = (vec2_t){ rz_f (tp), rz_f (tp + 4) };
			v->texcoord_idx = (int)i;
		}
	}
	if (o->weights && m->num_joints)
	{
		// per-position bone influences (u8 bone[4], f32 weight[3]), shared between equal sets
		mesh->position_node = CALLOC (cnt, sizeof (int));
		if (!mesh->position_node)
			return false;
		for (size_t i = 0; i < cnt; i++)
		{
			const u8 *w = d + o->weights + 16 * (size_t)soup[i][0];
			node_influence_t inf = { 0, 0 };
			influence_t wt[3];
			for (uint k = 0; k < 3; k++)
			{
				const float f = rz_f (w + 4 + 4 * k);
				if (w[k] != 0xff && w[k] < m->num_joints && f > 0)
					wt[inf.num_weights++] = (influence_t){ w[k], f };
			}
			int found = -1;
			for (size_t j = 0; j < m->num_node_influences && found < 0; j++)
			{
				const node_influence_t *e = m->node_influences + j;
				bool same = e->num_weights == inf.num_weights;
				for (size_t k = 0; same && k < e->num_weights; k++)
					same = e->weights[k].bone_idx == wt[k].bone_idx && e->weights[k].weight == wt[k].weight;
				if (same)
					found = (int)j;
			}
			if (found < 0 && inf.num_weights)
			{
				node_influence_t *ni = REALLOC (m->node_influences, (m->num_node_influences + 1) * sizeof (*ni));
				if (!ni)
					return false;
				m->node_influences = ni;
				ni += m->num_node_influences;
				ni->num_weights = inf.num_weights;
				ni->weights = MALLOC (inf.num_weights * sizeof (influence_t));
				if (!ni->weights)
					return false;
				memcpy (ni->weights, wt, inf.num_weights * sizeof (influence_t));
				found = (int)m->num_node_influences++;
			}
			mesh->position_node[i] = found;
		}
	}
	mesh->num_positions = mesh->num_vertices = cnt;
	mesh->num_normals = o->nn ? cnt : 0;
	mesh->num_texcoords = o->nuv ? cnt : 0;
	FREE (soup);

	material_t *nmat = REALLOC (m->materials, (m->num_materials + 1) * sizeof (*m->materials));
	if (!nmat)
		return false;
	m->materials = nmat;
	material_t *mt = m->materials + m->num_materials;
	memset (mt, 0, sizeof (*mt));
	mt->diffuse[0] = mt->diffuse[1] = mt->diffuse[2] = mt->diffuse[3] = 1.0f;
	snprintf (mt->name, sizeof (mt->name), "obj%u_b%u", k, batch);
	ccp tn = texname && tex_id ? texname (ctx, tex_id) : 0;
	if (tn && o->nuv)
	{
		snprintf (mt->textures[0], sizeof (mt->textures[0]), "%s", tn);
		mt->num_textures = 1;
		mt->wrap_s[0] = mt->wrap_t[0] = 1;
		mt->min_filter[0] = mt->mag_filter[0] = 1;
		mt->has_alpha = 1;
	}
	mesh->material_idx = (int)m->num_materials++;
	return true;
}

// The bone tree as joints (bind pose, no rotation): local offsets and the
// inverse of the bone's world translation.
static void rz_joints (model_t *m, const u8 *d, size_t *p, int parent, float wx, float wy, float wz)
{
	const uint idx = (uint)m->num_joints++;
	const size_t b = *p;
	joint_t *j = m->joints + idx;
	snprintf (j->name, sizeof (j->name), "bone%u", idx);
	j->parent_idx = parent;
	j->translate = (vec3_t){ rz_f (d + b), rz_f (d + b + 4), rz_f (d + b + 8) };
	j->scale = (vec3_t){ 1, 1, 1 };
	wx += j->translate.x, wy += j->translate.y, wz += j->translate.z;
	j->bind[0] = j->bind[5] = j->bind[10] = 1;
	j->bind[3] = wx, j->bind[7] = wy, j->bind[11] = wz;
	j->inverse_bind[0] = j->inverse_bind[5] = j->inverse_bind[10] = 1;
	j->inverse_bind[3] = -wx, j->inverse_bind[7] = -wy, j->inverse_bind[11] = -wz;
	j->has_inverse_bind = 1;
	const uint nc = rz_be32 (d + b + 16);
	*p += 32;
	for (uint i = 0; i < nc; i++)
		rz_joints (m, d, p, (int)idx, wx, wy, wz);
}

model_t *ParseRezModel (const u8 *d, size_t size, RezTexFunc texname, void *ctx)
{
	rz_tree_t t = { d, size, CALLOC (REZ_MAX_NODES, sizeof (rz_node_t)), 0, false };
	model_t *m = CALLOC (1, sizeof (*m));
	if (!t.nodes || !m)
	{
		FREE (t.nodes);
		FREE (m);
		return 0;
	}
	rz_walk (&t, 0, 0);	// a failed walk still leaves the objects read so far
	for (uint k = 0; k < t.n && !m->num_joints; k++)
		if (t.nodes[k].bones && t.nodes[k].nbones)
		{
			m->joints = CALLOC (t.nodes[k].nbones, sizeof (joint_t));
			if (m->joints)
			{
				size_t p = t.nodes[k].bones;
				rz_joints (m, d, &p, -1, 0, 0, 0);
			}
		}
	for (uint k = 0; k < t.n; k++)
	{
		const rz_node_t *o = t.nodes + k;
		if (!o->dlsz)
			continue;
		bool any = false;
		for (uint b = 0; b < o->nb; b++)
		{
			const u8 *e = d + o->bat + 20 * b;
			const size_t off = rz_be32 (e + 8), len = rz_be32 (e + 12);
			if (off > o->dlsz || len > o->dlsz - off)
				continue;
			any = true;
			if (!rz_mesh (m, &t, k, b, o->dl + off, len, rz_be32 (e), texname, ctx))
			{
				FREE (t.nodes);
				FreeModel (m);
				return 0;
			}
		}
		if (!any && !rz_mesh (m, &t, k, 0, o->dl, o->dlsz, 0, texname, ctx))
		{
			FREE (t.nodes);
			FreeModel (m);
			return 0;
		}
	}
	FREE (t.nodes);
	if (!m->num_meshes)
	{
		FreeModel (m);
		return 0;
	}
	return m;
}

// Animation tree: {u32 ?, u32 frames, f32 speed, u32 frames ptr, u32 children,
// u32 child ptr} + frames * 40 bytes {f32 pos[3], quat[4] xyzw, scale[3]} +
// children * u32 + the children (depth first).
static size_t rz_awalk (const u8 *a, size_t size, size_t p, size_t *nodes, uint *n, uint max)
{
	if (*n >= max || !rz_ok (size, p, 24))
		return (size_t)-1;
	const size_t nf = rz_be32 (a + p + 4), nc = rz_be32 (a + p + 16);
	if (nf > 0x10000 || nc > 256)
		return (size_t)-1;
	nodes[(*n)++] = p;
	size_t q = p + 24 + nf * 40 + nc * 4;
	if (!rz_ok (size, p, q - p))
		return (size_t)-1;
	for (size_t i = 0; i < nc; i++)
		if ((q = rz_awalk (a, size, q, nodes, n, max)) == (size_t)-1)
			return q;
	return q;
}

static void rz_qmul (const float *a, const float *b, float *o)
{
	o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

#define REZ_ANIM_FPS 30.0f

bool AddRezAnimation (model_t *m, const u8 *d, size_t size, const u8 *a, size_t asize)
{
	rz_tree_t t = { d, size, CALLOC (REZ_MAX_NODES, sizeof (rz_node_t)), 0, false };
	size_t *an = CALLOC (REZ_MAX_NODES, sizeof (size_t));
	if (!t.nodes || !an)
	{
		FREE (t.nodes);
		FREE (an);
		return false;
	}
	uint na = 0;
	rz_walk (&t, 0, 0);
	const bool ok = !m->num_joints && t.n && rz_awalk (a, asize, 0, an, &na, REZ_MAX_NODES) != (size_t)-1 && na == t.n;
	model_animation_t *anims = ok ? REALLOC (m->animations, (m->num_animations + 1) * sizeof (*anims)) : 0;
	if (!anims)
	{
		FREE (t.nodes);
		FREE (an);
		return false;
	}
	m->animations = anims;
	model_animation_t *an_out = m->animations + m->num_animations++;
	memset (an_out, 0, sizeof (*an_out));
	snprintf (an_out->name, sizeof (an_out->name), "anim");
	size_t nch = 0;
	for (size_t mi = 0; mi < m->num_meshes; mi++)
	{
		uint k = 0, b = 0;
		if (sscanf (m->meshes[mi].name, "obj%u_b%u", &k, &b) != 2 || k >= t.n)
			continue;
		const rz_node_t *o = t.nodes + k;
		const size_t nf = rz_be32 (a + an[k] + 4);
		if (!nf)
			continue;
		model_anim_channel_t *nc = REALLOC (an_out->channels, (nch + 3) * sizeof (*nc));
		if (!nc)
			break;
		an_out->channels = nc;
		model_anim_channel_t *tr = nc + nch, *ro = tr + 1, *sc = tr + 2;
		memset (tr, 0, 3 * sizeof (*tr));
		tr->node_idx = ro->node_idx = sc->node_idx = (int)mi;
		tr->path = MODEL_ANIM_TRANSLATION, ro->path = MODEL_ANIM_ROTATION, sc->path = MODEL_ANIM_SCALE;
		tr->count = ro->count = sc->count = nf;
		tr->components = sc->components = 3, ro->components = 4;
		tr->times = CALLOC (nf, sizeof (float));
		ro->times = CALLOC (nf, sizeof (float));
		sc->times = CALLOC (nf, sizeof (float));
		tr->values = CALLOC (nf, 3 * sizeof (float));
		ro->values = CALLOC (nf, 4 * sizeof (float));
		sc->values = CALLOC (nf, 3 * sizeof (float));
		nch += 3;
		an_out->num_channels = nch;
		if (!tr->times || !ro->times || !sc->times || !tr->values || !ro->values || !sc->values)
			continue;
		const float inv[4] = { -o->q[0], -o->q[1], -o->q[2], o->q[3] };
		for (size_t f = 0; f < nf; f++)
		{
			const u8 *fr = a + an[k] + 24 + 40 * f;
			float pos[3], q[4], s[3], rq[4], rs[3], rel[3];
			for (uint i = 0; i < 3; i++)
				pos[i] = rz_f (fr + 4 * i), s[i] = rz_f (fr + 28 + 4 * i);
			for (uint i = 0; i < 4; i++)
				q[i] = rz_f (fr + 12 + 4 * i);
			rz_qmul (q, inv, rq);
			for (uint i = 0; i < 3; i++)
			{
				rel[i] = pos[i] - o->t[i];
				rs[i] = o->s[i] ? s[i] / o->s[i] : 1.0f;
			}
			// M = T(rel + pivot) R S T(-pivot) = T(rel + pivot - R S pivot) R S
			float sp[3] = { rs[0] * o->pv[0], rs[1] * o->pv[1], rs[2] * o->pv[2] }, rot[3];
			// rotate SP by RQ (v' = v + 2w(u x v) + 2 u x (u x v))
			const float *u = rq, w = rq[3];
			const float c1[3] = { u[1] * sp[2] - u[2] * sp[1], u[2] * sp[0] - u[0] * sp[2], u[0] * sp[1] - u[1] * sp[0] };
			const float c2[3] = { u[1] * c1[2] - u[2] * c1[1], u[2] * c1[0] - u[0] * c1[2], u[0] * c1[1] - u[1] * c1[0] };
			for (uint i = 0; i < 3; i++)
				rot[i] = sp[i] + 2 * w * c1[i] + 2 * c2[i];
			tr->times[f] = ro->times[f] = sc->times[f] = f / REZ_ANIM_FPS;
			for (uint i = 0; i < 3; i++)
			{
				tr->values[3 * f + i] = rel[i] + o->pv[i] - rot[i];
				sc->values[3 * f + i] = rs[i];
			}
			for (uint i = 0; i < 4; i++)
				ro->values[4 * f + i] = rq[i];
		}
	}
	FREE (t.nodes);
	FREE (an);
	return true;
}

//-----------------------------------------------------------------------------
// skeletal motion (type 7)

model_t *CopyRezSkeleton (const model_t *m)
{
	if (!m || !m->num_joints)
		return 0;
	model_t *c = CALLOC (1, sizeof (*c));
	if (!c)
		return 0;
	c->joints = MALLOC (m->num_joints * sizeof (joint_t));
	if (!c->joints)
	{
		FREE (c);
		return 0;
	}
	memcpy (c->joints, m->joints, m->num_joints * sizeof (joint_t));
	c->num_joints = m->num_joints;
	return c;
}

model_t *ParseRezMotion (const u8 *d, size_t size, const model_t *ref)
{
	if (!ref || size < 0x40)
		return 0;
	const size_t nf = rz_be32 (d), nb = rz_be32 (d + 4), stride = rz_be32 (d + 12);
	if (nb != ref->num_joints || stride != 12 + 8 * nb || !nf || nf > 0x10000 || 0x14 + nf * stride > size)
		return 0;
	model_t *m = CopyRezSkeleton (ref);
	if (!m)
		return 0;
	m->animations = CALLOC (1, sizeof (*m->animations));
	m->animations->channels = CALLOC (nb + 1, sizeof (model_anim_channel_t));
	if (!m->animations || !m->animations->channels)
	{
		FreeModel (m);
		return 0;
	}
	m->num_animations = 1;
	snprintf (m->animations->name, sizeof (m->animations->name), "motion");
	model_animation_t *an = m->animations;
	for (size_t j = 0; j <= nb; j++)
	{
		// channel NB: root offset, others: rotation of bone J
		model_anim_channel_t *ch = an->channels + an->num_channels++;
		ch->node_idx = j == nb ? 0 : (int)j;
		ch->path = j == nb ? MODEL_ANIM_TRANSLATION : MODEL_ANIM_ROTATION;
		ch->components = j == nb ? 3 : 4;
		ch->count = nf;
		ch->times = CALLOC (nf, sizeof (float));
		ch->values = CALLOC (nf, ch->components * sizeof (float));
		if (!ch->times || !ch->values)
		{
			FreeModel (m);
			return 0;
		}
		float prev[4] = { 0, 0, 0, 1 };
		for (size_t f = 0; f < nf; f++)
		{
			const u8 *fr = d + 0x14 + f * stride;
			ch->times[f] = f / REZ_ANIM_FPS;
			if (j == nb)
			{
				const u8 *f0 = d + 0x14;
				for (uint i = 0; i < 3; i++)
					ch->values[3 * f + i] = m->joints[0].translate.x * (i == 0) + m->joints[0].translate.y * (i == 1)
						+ m->joints[0].translate.z * (i == 2) + rz_f (fr + 4 * i) - rz_f (f0 + 4 * i);
				continue;
			}
			float q[4], dot = 0;
			for (uint i = 0; i < 4; i++)
			{
				q[i] = (s16)rz_be16 (fr + 12 + 8 * j + 2 * i) / 32767.0f;
				dot += q[i] * prev[i];
			}
			for (uint i = 0; i < 4; i++)
			{
				if (dot < 0)
					q[i] = -q[i];
				ch->values[4 * f + i] = prev[i] = q[i];
			}
		}
	}
	return m;
}
