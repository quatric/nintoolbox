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
static bool rz_slots (const u8 *d, size_t size, const u8 **slots, uint *n)
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
	return true;
}

bool IsHumongousRez (const u8 *d, size_t size)
{
	const u8 *slots;
	uint n;
	if (!rz_slots (d, size, &slots, &n))
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
	if (type != REZ_TEXTURE && type != REZ_SOUND && type != REZ_VIDEO && type != REZ_MESH)
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
	uint n;
	if (!rz_slots (d, size, &slots, &n))
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
			if (rz_res (s, size, g, 0, &e) && (e.kind != REZ_VIDEO || (e.usize > 0x40 && !(e.flags & 1) && !memcmp (d + e.offset, "THP", 4))))
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
			if (rz_res (t + 16 + i * 24, size, g, i, &e) && e.kind != REZ_VIDEO)
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
// geometry (type 75)

static float rz_f (const u8 *p)
{
	const u32 u = rz_be32 (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

static bool rz_ok (size_t size, size_t off, size_t len) { return off <= size && len <= size - off; }

// Offset of a tagged pointer, or ~0.
static size_t rz_ptr (u32 v)
{
	return (v >> 16) == 0x010b && (v & 0xffff) >= 0x20 ? (v & 0xffff) - 0x20 : (size_t)-1;
}

static bool rz_object (const u8 *d, size_t size, size_t o, model_t *m, uint num, RezTexFunc texname, void *ctx)
{
	const u32 dlsz = rz_be32 (d + o + 8);
	const size_t dl = rz_ptr (rz_be32 (d + o + 0x0c)), pos = rz_ptr (rz_be32 (d + o + 0x14)),
		nrm = rz_ptr (rz_be32 (d + o + 0x24)), uv = rz_ptr (rz_be32 (d + o + 0x2c));
	const uint np = rz_be32 (d + o + 0x10), nn = rz_be32 (d + o + 0x20), nu = rz_be32 (d + o + 0x28);
	if (!dlsz || !np || !nn || !nu || dl == (size_t)-1 || pos == (size_t)-1 || nrm == (size_t)-1 || uv == (size_t)-1
		|| np > 0x4000 || nn > 0x4000 || nu > 0x4000 || !rz_ok (size, dl, dlsz) || !rz_ok (size, pos, np * 12)
		|| !rz_ok (size, nrm, nn * 12) || !rz_ok (size, uv, nu * 8))
		return false;
	const uint wp = np > 256 ? 2 : 1, wn = nn > 256 ? 2 : 1, wu = nu > 256 ? 2 : 1, vs = wp + wn + wu;
	const u8 *p = d + dl, *end = d + dl + dlsz;
	size_t cap = 3072, cnt = 0;
	uint (*soup)[3] = MALLOC (cap * sizeof (*soup));
	if (!soup)
		return true;
	uint idx[8192][3];
	while (p + 3 <= end && *p)
	{
		const uint cmd = *p & 0xf8;
		uint n = rz_be16 (p + 1);
		p += 3;
		if (n > 8192 || (size_t)(end - p) < (size_t)n * vs || (cmd != 0x80 && cmd != 0x90 && cmd != 0x98 && cmd != 0xa0))
			break;
		for (uint i = 0; i < n; i++)
		{
			idx[i][0] = wp == 2 ? rz_be16 (p) : p[0];
			p += wp;
			idx[i][1] = wn == 2 ? rz_be16 (p) : p[0];
			p += wn;
			idx[i][2] = wu == 2 ? rz_be16 (p) : p[0];
			p += wu;
			if (idx[i][0] >= np || idx[i][1] >= nn || idx[i][2] >= nu)
				n = 0;
		}
		if (!n)
			break;
		#define REMIT(a, b, c) do { \
			if (cnt + 3 > cap) { cap *= 2; uint (*ns)[3] = REALLOC (soup, cap * sizeof (*soup)); if (!ns) { FREE (soup); return true; } soup = ns; } \
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
		return true;
	}
	mesh_t *nm = REALLOC (m->meshes, (m->num_meshes + 1) * sizeof (*nm));
	if (!nm)
	{
		FREE (soup);
		return true;
	}
	m->meshes = nm;
	mesh_t *mesh = m->meshes + m->num_meshes++;
	memset (mesh, 0, sizeof (*mesh));
	snprintf (mesh->name, sizeof (mesh->name), "obj%u", num);
	mesh->vertices = CALLOC (cnt, sizeof (*mesh->vertices));
	mesh->positions = CALLOC (cnt, sizeof (*mesh->positions));
	mesh->normals = CALLOC (cnt, sizeof (*mesh->normals));
	mesh->texcoords = CALLOC (cnt, sizeof (*mesh->texcoords));
	if (!mesh->vertices || !mesh->positions || !mesh->normals || !mesh->texcoords)
	{
		FREE (soup);
		return false;
	}
	for (size_t i = 0; i < cnt; i++)
	{
		const u8 *pp = d + pos + 12 * (size_t)soup[i][0], *np_ = d + nrm + 12 * (size_t)soup[i][1],
			*tp = d + uv + 8 * (size_t)soup[i][2];
		mesh->positions[i] = (vec3_t){ rz_f (pp), rz_f (pp + 4), rz_f (pp + 8) };
		mesh->normals[i] = (vec3_t){ rz_f (np_), rz_f (np_ + 4), rz_f (np_ + 8) };
		mesh->texcoords[i] = (vec2_t){ rz_f (tp), rz_f (tp + 4) };
		vertex_t *v = mesh->vertices + i;
		v->position_idx = v->normal_idx = v->texcoord_idx = (int)i;
		v->tangent_idx = v->matrix_idx = -1;
		v->color_idx[0] = v->color_idx[1] = -1;
		for (int e = 0; e < 7; e++)
			v->extra_texcoord_idx[e] = -1;
	}
	mesh->num_positions = mesh->num_normals = mesh->num_texcoords = mesh->num_vertices = cnt;
	FREE (soup);

	m->materials = REALLOC (m->materials, (m->num_materials + 1) * sizeof (*m->materials));
	material_t *mt = m->materials + m->num_materials;
	memset (mt, 0, sizeof (*mt));
	mt->diffuse[0] = mt->diffuse[1] = mt->diffuse[2] = mt->diffuse[3] = 1.0f;
	snprintf (mt->name, sizeof (mt->name), "mat%u", num);
	ccp tn = texname ? texname (ctx) : 0;
	if (tn)
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

model_t *ParseRezMesh (const u8 *d, size_t size, RezTexFunc texname, void *ctx)
{
	model_t *m = CALLOC (1, sizeof (*m));
	if (!m)
		return 0;
	uint num = 0;
	for (size_t o = 0; o + 0x40 <= size; o += 4)
		if (rz_be32 (d + o) == 0x7843 && rz_object (d, size, o, m, num, texname, ctx))
		{
			num++;
			o += 0x3c;
		}
	if (!m->num_meshes)
	{
		FreeModel (m);
		return 0;
	}
	return m;
}
