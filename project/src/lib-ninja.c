// SPDX-License-Identifier: GPL-2.0+
// Illvelo (Wii) Sega "Ninja" chunk model/motion family -- see lib-ninja.h
// for exactly what is and is not decoded.

#include "lib-ninja.h"
#include "lib-nintendo.h"
#include <string.h>

#define NJ_CHUNK_HEADER_SIZE 8

static int nj_tag_printable (const u8 *tag)
{
	for (int i = 0; i < 4; i++)
		if (tag[i] < 0x20 || tag[i] > 0x7e)
			return 0;
	return 1;
}

int IsNinjaChunk (const u8 *data, size_t size)
{
	if (!data || size < NJ_CHUNK_HEADER_SIZE)
		return 0;
	return !memcmp (data, "LTJN", 4) // NJTL
		|| !memcmp (data, "MCJN", 4) // NJCM
		|| !memcmp (data, "MDMN", 4) // NMDM
		|| !memcmp (data, "MACN", 4); // NCAM (camera motion; seen as a lone .njm root)
}

// Decodes the confirmed NJTL (texture list) body: see lib-ninja.h for the
// byte-exact layout this was verified against.
static void nj_dump_njtl (FILE *f, const u8 *body, u32 body_size)
{
	if (body_size < 8)
	{
		fprintf (f, "  <NJTL body too small to hold header fields>\n");
		return;
	}
	const u32 texlist_off = rd_be32 (body);
	const u32 tex_count = rd_be32 (body + 4);
	fprintf (f, "  texlist_rel_off=0x%x tex_count=%u\n", texlist_off, tex_count);

	if ((u64)texlist_off + (u64)tex_count * 12 > body_size)
	{
		fprintf (f, "  <texture list entry array runs past chunk body, stopping>\n");
		return;
	}

	for (u32 i = 0; i < tex_count; i++)
	{
		const u8 *entry = body + texlist_off + (size_t)i * 12;
		const u32 name_off = rd_be32 (entry);
		const u32 global_index = rd_be32 (entry + 4);
		const u32 flags = rd_be32 (entry + 8);

		fprintf (f, "  [%u] global_index=0x%x flags=0x%x name=", i, global_index, flags);
		if (name_off < body_size)
		{
			const u8 *name = body + name_off;
			const u8 *end = body + body_size;
			const u8 *p = name;
			while (p < end && *p)
				p++;
			fprintf (f, "\"%.*s\"\n", (int)(p - name), name);
		}
		else
			fprintf (f, "<name_rel_off 0x%x out of range>\n", name_off);
	}
}

enumError DecodeNinjaChunk_Text (FILE *f, const u8 *data, size_t size)
{
	if (!f || !IsNinjaChunk (data, size))
		return EINVAL;

	fprintf (f, "# Illvelo (Wii) Sega \"Ninja\" chunk model/motion file\n");
	fprintf (f,
		"# reverse-engineered top-level chunk tree only: tags are\n"
		"# byte-reversed ASCII (\"LTJN\"=NJTL, \"MCJN\"=NJCM, \"MDMN\"=NMDM,\n"
		"# \"0FOP\"=POF0), all numeric fields are big-endian; only the NJTL\n"
		"# texture list is decoded, NJCM/NMDM mesh/motion payloads are not\n"
		"# (see lib-ninja.h)\n");

	size_t pos = 0;
	while (pos + NJ_CHUNK_HEADER_SIZE <= size)
	{
		const u8 *hdr = data + pos;
		if (!nj_tag_printable (hdr))
		{
			fprintf (f, "@0x%08zx <non-chunk data, %zu bytes remaining, not decoded>\n", pos,
				size - pos);
			break;
		}

		char tag[5];
		memcpy (tag, hdr, 4);
		tag[4] = 0;
		const u32 body_size = rd_be32 (hdr + 4);
		const u64 chunk_end = (u64)pos + NJ_CHUNK_HEADER_SIZE + body_size;

		fprintf (f, "@0x%08zx %-4s size=0x%x\n", pos, tag, body_size);
		if (chunk_end > size)
		{
			fprintf (f, "  <chunk size runs past end of file, stopping>\n");
			break;
		}

		if (!strcmp (tag, "LTJN"))
			nj_dump_njtl (f, hdr + NJ_CHUNK_HEADER_SIZE, body_size);
		else
			fprintf (f, "  (opaque chunk, not decoded further)\n");

		pos = (size_t)chunk_end;
	}

	return ERR_OK;
}


//-----------------------------------------------------------------------------
// NJCM chunk model -> model_t; see lib-ninja.h for the layout.

#include <math.h>

typedef struct
{
	const u8 *d;
	size_t size; // NJCM body size
	const u8 *tex_names[64];
	uint n_tex;
	model_t *model;
	int mat_of_tex[65]; // texture index (64 = untextured) -> material
	uint n_nodes;
} nj_ctx_t;

static uint nj_rd16 (const u8 *p)
{
	return p[0] << 8 | p[1];
}

static float nj_rdf (const u8 *p)
{
	u32 v = rd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return isfinite (f) ? f : 0.0f;
}

static void nj_mul (float *r, const float *a, const float *b) // column-major r = a * b
{
	float t[16];
	for (uint c = 0; c < 4; c++)
		for (uint rr = 0; rr < 4; rr++)
			t[c * 4 + rr] = a[rr] * b[c * 4] + a[4 + rr] * b[c * 4 + 1] + a[8 + rr] * b[c * 4 + 2]
				+ a[12 + rr] * b[c * 4 + 3];
	memcpy (r, t, sizeof (t));
}

static void nj_rot (float *m, int axis, float rad)
{
	const float c = cosf (rad), s = sinf (rad);
	memset (m, 0, 16 * sizeof (float));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
	if (axis == 0)
		m[5] = c, m[6] = s, m[9] = -s, m[10] = c;
	else if (axis == 1)
		m[0] = c, m[2] = -s, m[8] = s, m[10] = c;
	else
		m[0] = c, m[1] = s, m[4] = -s, m[5] = c;
}

static int nj_material (nj_ctx_t *c, uint tex)
{
	const uint slot = tex < c->n_tex ? tex : 64;
	if (c->mat_of_tex[slot] >= 0)
		return c->mat_of_tex[slot];
	model_t *m = c->model;
	material_t *nm = REALLOC (m->materials, (m->num_materials + 1) * sizeof (*nm));
	if (!nm)
		return 0;
	m->materials = nm;
	material_t *mt = m->materials + m->num_materials;
	memset (mt, 0, sizeof (*mt));
	mt->diffuse[0] = mt->diffuse[1] = mt->diffuse[2] = mt->diffuse[3] = 1.0f;
	if (slot < 64 && c->tex_names[slot])
	{
		snprintf (mt->name, sizeof (mt->name), "%s", (ccp)c->tex_names[slot]);
		snprintf (mt->textures[0], sizeof (mt->textures[0]), "%s", (ccp)c->tex_names[slot]);
		mt->num_textures = 1;
		mt->wrap_s[0] = mt->wrap_t[0] = 1;
		mt->min_filter[0] = mt->mag_filter[0] = 1;
		mt->has_alpha = 1;
	}
	else
		snprintf (mt->name, sizeof (mt->name), "untextured");
	c->mat_of_tex[slot] = (int)m->num_materials++;
	return c->mat_of_tex[slot];
}

static bool nj_add_model (nj_ctx_t *c, u32 mo, const float *W, uint node)
{
	const u8 *d = c->d;
	if ((u64)mo + 0x18 > c->size)
		return true;
	const u32 vl = rd_be32 (d + mo), pl = rd_be32 (d + mo + 4);
	if (!vl || !pl || (u64)vl + 8 > c->size || pl >= c->size)
		return true;
	const uint vtype = nj_rd16 (d + vl);
	const u32 count = rd_be32 (d + vl + 4);
	const uint stride = vtype == 0x8023 ? 16 : vtype == 0x802a ? 28 : 0;
	if (!stride || !count || count > 0x100000 || (u64)vl + 8 + (u64)count * stride > c->size)
		return true;

	mesh_t tmp;
	memset (&tmp, 0, sizeof (tmp));
	tmp.positions = CALLOC (count, sizeof (vec3_t));
	tmp.normals = stride == 28 ? CALLOC (count, sizeof (vec3_t)) : 0;
	if (!tmp.positions || (stride == 28 && !tmp.normals))
	{
		FREE (tmp.positions);
		FREE (tmp.normals);
		return false;
	}
	tmp.num_positions = count;
	tmp.num_normals = stride == 28 ? count : 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *v = d + vl + 8 + (size_t)i * stride;
		const float x = nj_rdf (v), y = nj_rdf (v + 4), z = nj_rdf (v + 8);
		tmp.positions[i].x = x * W[0] + y * W[4] + z * W[8] + W[12];
		tmp.positions[i].y = x * W[1] + y * W[5] + z * W[9] + W[13];
		tmp.positions[i].z = x * W[2] + y * W[6] + z * W[10] + W[14];
		if (stride == 28)
		{
			const float nx = nj_rdf (v + 12), ny = nj_rdf (v + 16), nz = nj_rdf (v + 20);
			tmp.normals[i].x = nx * W[0] + ny * W[4] + nz * W[8];
			tmp.normals[i].y = nx * W[1] + ny * W[5] + nz * W[9];
			tmp.normals[i].z = nx * W[2] + ny * W[6] + nz * W[10];
		}
	}

	// polygon list -> triangle soup
	size_t cap = 0, num = 0;
	vertex_t *verts = 0;
	vec2_t *uvs = 0;
	int *tri_mat = 0;
	size_t pos = pl;
	int cur_mat = nj_material (c, 64);
	bool ok = true;
	while (ok && pos + 2 <= c->size)
	{
		const uint h = nj_rd16 (d + pos);
		if (h == 0x00ff)
			break;
		if ((h >> 8) == 0x25 && pos + 4 <= c->size)
		{
			pos += 4 + 2 * nj_rd16 (d + pos + 2);
			continue;
		}
		if (h != 0x3408 || pos + 10 > c->size)
			break;
		const uint tex = nj_rd16 (d + pos + 2);
		const uint words = nj_rd16 (d + pos + 6), nstrips = nj_rd16 (d + pos + 8);
		const size_t end = pos + 8 + 2 * (size_t)words;
		if (end > c->size)
			break;
		cur_mat = nj_material (c, (tex & 0x3fff) < 64 ? tex & 0x3fff : 64);
		size_t p = pos + 10;
		for (uint s = 0; s < nstrips && p + 2 <= end; s++)
		{
			int len = (s16)nj_rd16 (d + p);
			p += 2;
			const uint n = len < 0 ? (uint)-len : (uint)len;
			if (p + 6ull * n > end)
				break;
			for (uint t = 0; t + 2 < n; t++)
			{
				uint i3[3] = { t, t + 1, t + 2 };
				const bool flip = (len < 0) != ((t & 1) != 0);
				if (flip)
					i3[0] = t + 1, i3[1] = t;
				bool good = true;
				for (uint k = 0; k < 3; k++)
					good = good && nj_rd16 (d + p + 6 * i3[k]) < count;
				if (!good)
					continue;
				if (num + 3 > cap)
				{
					cap = cap ? cap * 2 : 768;
					vertex_t *nv = REALLOC (verts, cap * sizeof (*nv));
					vec2_t *nu = REALLOC (uvs, cap * sizeof (*nu));
					int *nt = REALLOC (tri_mat, (cap / 3 + 1) * sizeof (*nt));
					if (nv)
						verts = nv;
					if (nu)
						uvs = nu;
					if (nt)
						tri_mat = nt;
					if (!nv || !nu || !nt)
					{
						ok = false;
						break;
					}
				}
				for (uint k = 0; k < 3; k++)
				{
					const u8 *e = d + p + 6 * i3[k];
					vertex_t *v = verts + num;
					memset (v, 0, sizeof (*v));
					v->position_idx = (int)nj_rd16 (e);
					v->normal_idx = stride == 28 ? v->position_idx : -1;
					v->tangent_idx = v->matrix_idx = -1;
					v->color_idx[0] = v->color_idx[1] = -1;
					for (int x = 0; x < 7; x++)
						v->extra_texcoord_idx[x] = -1;
					uvs[num].u = nj_rd16 (e + 2) / 1024.0f;
					uvs[num].v = nj_rd16 (e + 4) / 1024.0f;
					v->texcoord_idx = (int)num;
					num++;
				}
				tri_mat[num / 3 - 1] = cur_mat;
			}
			if (!ok)
				break;
			p += 6ull * n;
		}
		pos = end;
	}
	if (!ok || !num)
	{
		FREE (tmp.positions);
		FREE (tmp.normals);
		FREE (verts);
		FREE (uvs);
		FREE (tri_mat);
		return ok;
	}

	model_t *model = c->model;
	mesh_t *nm = REALLOC (model->meshes, (model->num_meshes + 1) * sizeof (*nm));
	if (!nm)
	{
		FREE (tmp.positions);
		FREE (tmp.normals);
		FREE (verts);
		FREE (uvs);
		FREE (tri_mat);
		return false;
	}
	model->meshes = nm;
	mesh_t *mesh = model->meshes + model->num_meshes++;
	*mesh = tmp;
	snprintf (mesh->name, sizeof (mesh->name), "node%u", node);
	mesh->vertices = verts;
	mesh->num_vertices = num;
	mesh->texcoords = uvs;
	mesh->num_texcoords = num;
	mesh->triangle_materials = tri_mat;
	mesh->material_idx = tri_mat[0];
	return true;
}

static bool nj_walk (nj_ctx_t *c, u32 off, const float *parent, uint depth)
{
	const u8 *d = c->d;
	while (true)
	{
		if (depth > 64 || c->n_nodes++ > 0x10000 || (u64)off + 0x34 > c->size)
			return true;
		const u32 ef = rd_be32 (d + off), mdl = rd_be32 (d + off + 4);
		float L[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, S[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		if (!(ef & 4))
		{
			S[0] = nj_rdf (d + off + 32);
			S[5] = nj_rdf (d + off + 36);
			S[10] = nj_rdf (d + off + 40);
		}
		if (!(ef & 2))
		{
			float rx[16], ry[16], rz[16], t[16];
			const float k = 6.2831853f / 65536.0f;
			nj_rot (rx, 0, (s32)rd_be32 (d + off + 20) * k);
			nj_rot (ry, 1, (s32)rd_be32 (d + off + 24) * k);
			nj_rot (rz, 2, (s32)rd_be32 (d + off + 28) * k);
			if (ef & 0x20) // ZXY: Z first, then X, then Y
			{
				nj_mul (t, ry, rx);
				nj_mul (L, t, rz);
			}
			else // XYZ: X first, then Y, then Z
			{
				nj_mul (t, rz, ry);
				nj_mul (L, t, rx);
			}
		}
		float RS[16];
		nj_mul (RS, L, S);
		if (!(ef & 1))
		{
			RS[12] = nj_rdf (d + off + 8);
			RS[13] = nj_rdf (d + off + 12);
			RS[14] = nj_rdf (d + off + 16);
		}
		float W[16];
		if (parent)
			nj_mul (W, parent, RS);
		else
			memcpy (W, RS, sizeof (W));
		if (mdl && !(ef & 8) && !nj_add_model (c, mdl, W, c->n_nodes))
			return false;
		const u32 child = rd_be32 (d + off + 44), sib = rd_be32 (d + off + 48);
		if (child && !(ef & 0x10) && !nj_walk (c, child, W, depth + 1))
			return false;
		if (!sib)
			return true;
		off = sib;
	}
}

model_t *ParseNinjaModel (const u8 *data, size_t size)
{
	if (!data || size < 16)
		return 0;
	nj_ctx_t *c = CALLOC (1, sizeof (*c));
	model_t *model = CALLOC (1, sizeof (*model));
	if (!c || !model)
	{
		FREE (c);
		FREE (model);
		return 0;
	}
	c->model = model;
	for (uint i = 0; i < 65; i++)
		c->mat_of_tex[i] = -1;

	const u8 *njcm = 0;
	size_t pos = 0;
	while (pos + 8 <= size)
	{
		const u32 body = rd_be32 (data + pos + 4);
		if (pos + 8 + (u64)body > size)
			break;
		const u8 *b = data + pos + 8;
		if (!memcmp (data + pos, "LTJN", 4) && body >= 8)
		{
			const u32 tl = rd_be32 (b), n = rd_be32 (b + 4);
			if (n <= 64 && (u64)tl + 12ull * n <= body)
				for (uint i = 0; i < n; i++)
				{
					const u32 no = rd_be32 (b + tl + 12 * i);
					if (no < body && memchr (b + no, 0, body - no))
						c->tex_names[i] = b + no;
				}
			c->n_tex = n <= 64 ? n : 0;
		}
		else if (!memcmp (data + pos, "MCJN", 4) && !njcm)
		{
			njcm = b;
			c->size = body;
		}
		pos += 8 + (size_t)body;
	}
	bool ok = false;
	if (njcm)
	{
		c->d = njcm;
		ok = nj_walk (c, 0, 0, 0);
	}
	FREE (c);
	if (!ok || !model->num_meshes)
	{
		FreeModel (model);
		return 0;
	}
	return model;
}
