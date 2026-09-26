// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Atomic Planet PUB packages; see lib-pub.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-pub.h"
#include "lib-excite.h"
#include <string.h>

static u32 pb_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
static u32 pb_be16 (const u8 *p)
{
	return p[0] << 8 | p[1];
}

bool IsAtomicPub (const u8 *d, size_t size)
{
	if (size < 0x60 || pb_be32 (d) != 1 || pb_be32 (d + 4) != 1 || pb_be32 (d + 8) != size - 0x20
		|| pb_be32 (d + 0x20) != 1)
		return false;
	const size_t n = pb_be32 (d + 0x24), tbl = pb_be32 (d + 0x28);
	return n && n < 0x100000 && tbl >= 0x40 && tbl + n * 28 <= size;
}

pub_texture_t *ListPubTextures (const u8 *d, size_t size, uint *count)
{
	*count = 0;
	if (!IsAtomicPub (d, size))
		return 0;
	const uint n = pb_be32 (d + 0x24);
	const size_t tbl = pb_be32 (d + 0x28);
	pub_texture_t *list = MALLOC (n * sizeof (*list));
	if (!list)
		return 0;
	for (uint i = 0; i < n; i++)
	{
		const u8 *e = d + tbl + (size_t)i * 28;
		const size_t off = pb_be32 (e + 4), sz = pb_be32 (e + 12);
		if (pb_be16 (e + 8) != 1 || off < 0x40 || sz < 0x30 || off + sz > size)
			continue;
		const u8 *o = d + off;
		pub_texture_t t;
		memset (&t, 0, sizeof (t));
		t.hash = pb_be32 (e);
		t.gx_format = o[0x21];
		t.pal_format = o[0x22];
		t.pal_count = pb_be16 (o + 0x1c);
		t.height = pb_be16 (o + 0x2c);
		t.width = pb_be16 (o + 0x2e);
		const size_t img = pb_be32 (o + 0x24), isz = pb_be32 (o + 0x28), pal = pb_be32 (o + 0x18);
		if (!o[0x20] || !t.width || !t.height || t.width > 4096 || t.height > 4096 || !isz
			|| img + isz > sz)
			continue;
		if (t.gx_format == 9)
		{
			if (!t.pal_count || pal + 2 * t.pal_count > sz)
				continue;
			t.palette = o + pal;
		}
		else if (t.gx_format != 6 && t.gx_format != 1)
			continue;
		t.pixels = o + img;
		t.pixel_size = isz;
		list[(*count)++] = t;
	}
	return list;
}

enumError DecodePubTexture (u8 **rgba, const pub_texture_t *t)
{
	return DecodeGXTexture_RGBA (rgba, t->width, t->height, t->gx_format, t->pixels,
		t->pixel_size > 0xffffffffu ? 0xffffffffu : (uint)t->pixel_size, t->palette, t->pal_count,
		t->pal_format);
}

//-----------------------------------------------------------------------------
// meshes

pub_object_t *ListPubMeshes (const u8 *d, size_t size, uint *count)
{
	*count = 0;
	if (!IsAtomicPub (d, size))
		return 0;
	const uint n = pb_be32 (d + 0x24);
	const size_t tbl = pb_be32 (d + 0x28);
	pub_object_t *list = MALLOC (n * sizeof (*list));
	if (!list)
		return 0;
	for (uint i = 0; i < n; i++)
	{
		const u8 *e = d + tbl + (size_t)i * 28;
		const size_t off = pb_be32 (e + 4), sz = pb_be32 (e + 12);
		if (pb_be16 (e + 8) != 8 || off < 0x40 || sz < 0x100 || off + sz > size)
			continue;
		list[*count].hash = pb_be32 (e);
		list[*count].offset = off;
		list[*count].size = sz;
		(*count)++;
	}
	return list;
}

typedef struct
{
	uint slot;
	u32 data;
	uint count, off, stride, size;
} pub_attr_t;

static bool pb_ok (size_t size, size_t off, size_t len)
{
	return off <= size && len <= size - off;
}

// First attribute record of slot S of the geometry struct at G.
static bool pb_attr (const u8 *o, size_t size, u32 g, uint slot, pub_attr_t *a)
{
	if (!pb_ok (size, g, 0x10) || pb_be16 (o + g + 8) <= slot)
		return false;
	const u32 s = pb_be32 (o + g + 12 + 4 * slot);
	if (!pb_ok (size, s, 12))
		return false;
	const uint cnt = pb_be16 (o + s + 8);
	const u32 list = pb_be32 (o + s + 4);
	if (!cnt || !pb_ok (size, list, 4))
		return false;
	const u32 r = pb_be32 (o + list);
	if (!pb_ok (size, r, 16))
		return false;
	a->slot = slot;
	a->data = pb_be32 (o + r + 4);
	a->count = pb_be16 (o + r + 8);
	a->off = o[r + 10];
	a->stride = o[r + 11];
	a->size = o[r + 12];
	return a->count && a->stride && (uint)a->off + a->size <= a->stride
		&& pb_ok (size, a->data, (size_t)a->count * a->stride);
}

static float pb_f (const u8 *p)
{
	const u32 u = pb_be32 (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

static bool pb_part (const u8 *o, size_t size, model_t *m, uint part, u32 A, u32 B, u32 G,
	PubTexFunc texname, void *ctx)
{
	if (!pb_ok (size, A, 12) || !pb_ok (size, B, 16))
		return true;
	const u32 dl = pb_be32 (o + A + 4), dlsz = pb_be32 (o + A + 8);
	if (!dlsz || !pb_ok (size, dl, dlsz))
		return true;
	pub_attr_t at[4];
	bool has[4];
	for (uint s = 0; s < 4; s++)
		has[s] = pb_attr (o, size, G, s, at + s);
	// the position must be 3 x f32
	if (!has[0] || at[0].size != 12)
		return true;
	// tuple layout: GX order position, normal, colour, texcoord
	static const uint order[4] = { 0, 3, 1, 2 };
	uint tuple[4], nt = 0;
	for (uint k = 0; k < 4; k++)
		if (has[order[k]])
			tuple[nt++] = order[k];
	const u8 *p = o + dl, *end = o + dl + dlsz;
	size_t cap = 1024, num = 0;
	u32 *soup = MALLOC (cap * 3 * sizeof (u32) * nt);
	if (!soup)
		return false;
	while (p + 3 <= end && *p)
	{
		const uint cmd = *p & 0xf8, n = pb_be16 (p + 1);
		p += 3;
		if ((size_t)(end - p) < (size_t)n * 2 * nt
			|| (cmd != 0x80 && cmd != 0x90 && cmd != 0x98 && cmd != 0xa0))
			break;
		u32 idx[4096][4];
		if (n > 4096)
			break;
		for (uint i = 0; i < n; i++)
			for (uint k = 0; k < nt; k++, p += 2)
				idx[i][k] = pb_be16 (p);
// triangles as index tuples
#define EMIT(a, b, c)                                                                              \
	do                                                                                             \
	{                                                                                              \
		if (num + 3 > cap)                                                                         \
		{                                                                                          \
			cap *= 2;                                                                              \
			u32 *ns = REALLOC (soup, cap * 3 * sizeof (u32) * nt);                                 \
			if (!ns)                                                                               \
			{                                                                                      \
				FREE (soup);                                                                       \
				return false;                                                                      \
			}                                                                                      \
			soup = ns;                                                                             \
		}                                                                                          \
		for (uint k = 0; k < nt; k++)                                                              \
		{                                                                                          \
			soup[(num + 0) * nt + k] = idx[a][k];                                                  \
			soup[(num + 1) * nt + k] = idx[b][k];                                                  \
			soup[(num + 2) * nt + k] = idx[c][k];                                                  \
		}                                                                                          \
		num += 3;                                                                                  \
	} while (0)
		if (cmd == 0x90)
			for (uint i = 0; i + 2 < n; i += 3)
				EMIT (i, i + 1, i + 2);
		else if (cmd == 0x80)
			for (uint i = 0; i + 3 < n; i += 4)
			{
				EMIT (i, i + 1, i + 2);
				EMIT (i, i + 2, i + 3);
			}
		else if (cmd == 0x98)
			for (uint i = 0; i + 2 < n; i++)
			{
				if (idx[i][0] == idx[i + 1][0] || idx[i + 1][0] == idx[i + 2][0]
					|| idx[i][0] == idx[i + 2][0])
					continue;
				if (i & 1)
					EMIT (i + 1, i, i + 2);
				else
					EMIT (i, i + 1, i + 2);
			}
		else
			for (uint i = 1; i + 1 < n; i++)
				EMIT (0, i, i + 1);
#undef EMIT
	}
	if (!num)
	{
		FREE (soup);
		return true;
	}

	mesh_t *nm = REALLOC (m->meshes, (m->num_meshes + 1) * sizeof (*nm));
	if (!nm)
	{
		FREE (soup);
		return false;
	}
	m->meshes = nm;
	mesh_t *mesh = m->meshes + m->num_meshes++;
	memset (mesh, 0, sizeof (*mesh));
	snprintf (mesh->name, sizeof (mesh->name), "part%u", part);
	mesh->vertices = CALLOC (num, sizeof (*mesh->vertices));
	mesh->positions = CALLOC (num, sizeof (*mesh->positions));
	const bool hn = has[3], ht = has[2];
	if (hn)
		mesh->normals = CALLOC (num, sizeof (*mesh->normals));
	if (ht)
		mesh->texcoords = CALLOC (num, sizeof (*mesh->texcoords));
	if (!mesh->vertices || !mesh->positions || (hn && !mesh->normals) || (ht && !mesh->texcoords))
	{
		FREE (soup);
		return false;
	}
	for (size_t i = 0; i < num; i++)
	{
		uint pos_i = 0, nrm_i = 0, uv_i = 0;
		for (uint k = 0; k < nt; k++)
		{
			const uint v = soup[i * nt + k];
			if (tuple[k] == 0)
				pos_i = v;
			else if (tuple[k] == 3)
				nrm_i = v;
			else if (tuple[k] == 2)
				uv_i = v;
		}
		if (pos_i >= at[0].count || (hn && nrm_i >= at[3].count) || (ht && uv_i >= at[2].count))
			pos_i = nrm_i = uv_i = 0;
		const u8 *pp = o + at[0].data + (size_t)pos_i * at[0].stride + at[0].off;
		mesh->positions[i].x = pb_f (pp), mesh->positions[i].y = pb_f (pp + 4),
		mesh->positions[i].z = pb_f (pp + 8);
		vertex_t *v = mesh->vertices + i;
		v->position_idx = (int)i;
		v->normal_idx = v->tangent_idx = v->matrix_idx = v->texcoord_idx = -1;
		v->color_idx[0] = v->color_idx[1] = -1;
		for (int e = 0; e < 7; e++)
			v->extra_texcoord_idx[e] = -1;
		if (hn)
		{
			const u8 *np = o + at[3].data + (size_t)nrm_i * at[3].stride + at[3].off;
			if (at[3].size == 12)
				mesh->normals[i] = (vec3_t) { pb_f (np), pb_f (np + 4), pb_f (np + 8) };
			else
				mesh->normals[i] = (vec3_t) { (s16)pb_be16 (np) / 16384.0f,
					(s16)pb_be16 (np + 2) / 16384.0f, (s16)pb_be16 (np + 4) / 16384.0f };
			v->normal_idx = (int)i;
		}
		if (ht)
		{
			const u8 *tp = o + at[2].data + (size_t)uv_i * at[2].stride + at[2].off;
			mesh->texcoords[i].u = (s16)pb_be16 (tp) / 1024.0f;
			mesh->texcoords[i].v = (s16)pb_be16 (tp + 2) / 1024.0f;
			v->texcoord_idx = (int)i;
		}
	}
	mesh->num_positions = mesh->num_vertices = num;
	mesh->num_normals = hn ? num : 0;
	mesh->num_texcoords = ht ? num : 0;
	FREE (soup);

	// material: first texture hash
	m->materials = REALLOC (m->materials, (m->num_materials + 1) * sizeof (*m->materials));
	material_t *mt = m->materials + m->num_materials;
	memset (mt, 0, sizeof (*mt));
	mt->diffuse[0] = mt->diffuse[1] = mt->diffuse[2] = mt->diffuse[3] = 1.0f;
	const u32 hash = pb_be32 (o + B + 12);
	ccp tn = hash && texname ? texname (ctx, hash) : 0;
	snprintf (mt->name, sizeof (mt->name), "mat%u", part);
	if (tn && ht)
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

model_t *ParsePubMesh (const u8 *o, size_t size, PubTexFunc texname, void *ctx)
{
	if (size < 0x100 || pb_be32 (o + 0x70) != 0x74)
		return 0;
	const u32 R = 0x74;
	const uint n = pb_be16 (o + R + 0x10);
	const u32 la = pb_be32 (o + R + 4), lb = pb_be32 (o + R + 8), lc = pb_be32 (o + R + 12);
	if (!n || n > 512 || !pb_ok (size, la, n * 4) || !pb_ok (size, lb, n * 4)
		|| !pb_ok (size, lc, n * 4))
		return 0;
	model_t *m = CALLOC (1, sizeof (*m));
	if (!m)
		return 0;
	for (uint i = 0; i < n; i++)
		if (!pb_part (o, size, m, i, pb_be32 (o + la + 4 * i), pb_be32 (o + lb + 4 * i),
				pb_be32 (o + lc + 4 * i), texname, ctx))
		{
			FreeModel (m);
			return 0;
		}
	if (!m->num_meshes)
	{
		FreeModel (m);
		return 0;
	}
	return m;
}
