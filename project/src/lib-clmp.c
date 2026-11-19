// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-clmp.h"
#include <string.h>
#include <math.h>
#undef calloc
#undef malloc
#undef realloc
#undef free

static inline uint cl16 (const u8 *p) { return p[0] << 8 | p[1]; }
static inline u32 cl32 (const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

typedef struct
{
	u32 off; // absolute file offset of the array
	uint count, frac, comps;
	bool ok;
} cl_arr_t;

static cl_arr_t cl_desc (const u8 *d, size_t size, size_t base, u32 doff)
{
	cl_arr_t a = { 0 };
	if (base + doff + 8 > size)
		return a;
	const u8 *p = d + base + doff;
	a.off = (u32)base + cl32 (p);
	a.count = cl16 (p + 4);
	a.frac = p[6] & 15;
	a.comps = p[7];
	a.ok = p[6] >> 4 == 3 && a.comps >= 2 && a.comps <= 8 && a.off + (size_t)a.count * a.comps * 2 <= size;
	return a;
}

// a mesh block starts with the word 0x18 and its position descriptor is {0x24, n, 0x3e, 6, 0}
static bool cl_block_ok (const u8 *d, size_t size, size_t b)
{
	if (b + 0x40 > size || cl32 (d + b) != 0x18 || cl32 (d + b + 0x18) != 0x24)
		return false;
	const cl_arr_t p = cl_desc (d, size, b, 0x18);
	return p.ok && p.comps == 6 && p.count > 0;
}

bool IsClmpModel (const u8 *d, size_t size)
{
	if (size < 0x100 || memcmp (d, "CLMP", 4))
		return false;
	for (size_t b = 0; b + 0x40 <= size; b += 4)
		if (cl_block_ok (d, size, b))
			return true;
	return false;
}

typedef struct
{
	uint pos, nrm, t0;
} cl_vtx_t;

static int cl_material (model_t *m, uint id)
{
	char name[64];
	snprintf (name, sizeof (name), "material_%u", id);
	for (uint i = 0; i < m->num_materials; i++)
		if (!strcmp (m->materials[i].name, name))
			return (int)i;
	material_t *nm = realloc (m->materials, (m->num_materials + 1) * sizeof (*nm));
	if (!nm)
		return 0;
	m->materials = nm;
	material_t *mat = m->materials + m->num_materials;
	memset (mat, 0, sizeof (*mat));
	snprintf (mat->name, sizeof (mat->name), "%s", name);
	mat->diffuse[0] = mat->diffuse[1] = mat->diffuse[2] = mat->diffuse[3] = 1.0f;
	return (int)m->num_materials++;
}

static void cl_flush (model_t *m, const u8 *d, const cl_arr_t *pa, const cl_arr_t *na, const cl_arr_t *ta, int mat,
	const cl_vtx_t *c, size_t n, uint no)
{
	if (!n)
		return;
	mesh_t *nm = realloc (m->meshes, (m->num_meshes + 1) * sizeof (*nm));
	if (!nm)
		return;
	m->meshes = nm;
	mesh_t *mesh = m->meshes + m->num_meshes;
	memset (mesh, 0, sizeof (*mesh));
	snprintf (mesh->name, sizeof (mesh->name), "mesh_%u", no);
	mesh->vertices = calloc (n, sizeof (vertex_t));
	mesh->positions = calloc (n, sizeof (vec3_t));
	mesh->position_node = calloc (n, sizeof (int));
	mesh->normals = calloc (n, sizeof (vec3_t));
	mesh->texcoords = calloc (n, sizeof (vec2_t));
	if (!mesh->vertices || !mesh->positions || !mesh->position_node || !mesh->normals || !mesh->texcoords)
	{
		free (mesh->vertices);
		free (mesh->positions);
		free (mesh->position_node);
		free (mesh->normals);
		free (mesh->texcoords);
		return;
	}
	const float ps = 1.0f / (float)(1u << pa->frac), ns = 1.0f / (float)(1u << na->frac),
				us = ta->ok ? 1.0f / (float)(1u << ta->frac) : 1.0f;
	for (size_t i = 0; i < n; i++)
	{
		vertex_t *v = mesh->vertices + i;
		v->tangent_idx = v->matrix_idx = -1;
		v->color_idx[0] = v->color_idx[1] = -1;
		for (int e = 0; e < 7; e++)
			v->extra_texcoord_idx[e] = -1;
		v->position_idx = v->normal_idx = v->texcoord_idx = (int)i;
		mesh->position_node[i] = -1;
		if (c[i].pos < pa->count)
		{
			const u8 *q = d + pa->off + (size_t)c[i].pos * pa->comps * 2;
			mesh->positions[i] = (vec3_t) { (s16)cl16 (q) * ps, (s16)cl16 (q + 2) * ps, (s16)cl16 (q + 4) * ps };
		}
		mesh->normals[i] = (vec3_t) { 0, 1, 0 };
		if (c[i].nrm < na->count)
		{
			const u8 *q = d + na->off + (size_t)c[i].nrm * na->comps * 2;
			const float x = (s16)cl16 (q) * ns, y = (s16)cl16 (q + 2) * ns, z = (s16)cl16 (q + 4) * ns;
			const float len = sqrtf (x * x + y * y + z * z);
			if (len > 0)
				mesh->normals[i] = (vec3_t) { x / len, y / len, z / len };
		}
		if (ta->ok && c[i].t0 < ta->count)
		{
			const u8 *q = d + ta->off + (size_t)c[i].t0 * ta->comps * 2;
			mesh->texcoords[i] = (vec2_t) { (s16)cl16 (q) * us, (s16)cl16 (q + 2) * us };
		}
	}
	mesh->num_positions = mesh->num_normals = mesh->num_texcoords = mesh->num_vertices = n;
	mesh->material_idx = mat;
	m->num_meshes++;
}

model_t *BuildClmpModel (const u8 *d, size_t size)
{
	if (size < 0x100 || memcmp (d, "CLMP", 4))
		return 0;
	model_t *m = calloc (1, sizeof (*m));
	if (!m)
		return 0;
	uint meshno = 0;
	for (size_t b = 0; b + 0x40 <= size; b += 4)
	{
		if (!cl_block_ok (d, size, b))
			continue;
		const cl_arr_t pa = cl_desc (d, size, b, cl32 (d + b));
		const cl_arr_t na = cl_desc (d, size, b, cl32 (d + b + 12));
		const u32 texd = cl32 (d + b + 8);
		const uint ntex = d[b + 0x14];
		cl_arr_t ta = { 0 };
		if (ntex)
			ta = cl_desc (d, size, b, texd);
		const u32 cl = cl32 (d + b + 0x10);
		if (!pa.ok || !na.ok || b + cl + 12 > size)
			continue;
		const u32 eoff = cl32 (d + b + cl + 4);
		const uint ne = cl16 (d + b + cl + 8);
		if (b + eoff + 16ull * ne > size)
			continue;
		uint fmt = 0;
		int mat = 0;
		for (uint k = 0; k < ne; k++)
		{
			const u8 *e = d + b + eoff + 16 * k;
			const uint type = e[0];
			const u32 val = cl32 (e + 4), dlo = cl32 (e + 8), dls = cl32 (e + 12);
			if (type == 3)
				fmt = val;
			else if (type == 1)
				mat = cl_material (m, val & 0x1fff);
			if (!dlo || !dls || b + dlo + dls > size)
				continue;
			// vertex layout: position, normal, colour 0, colour 1, texcoord 0, 1, ...
			uint w[8], vsz = (fmt & 3) ? 1 : 0;
			bool ok = (fmt & 3) <= 1;
			for (uint a = 0; a < 8; a++)
			{
				const uint k2 = fmt >> (2 + 2 * a) & 3;
				w[a] = k2 < 2 ? 0 : k2 - 1;
				ok = ok && k2 != 1;
				vsz += w[a];
			}
			if (!ok || !w[0] || !vsz)
				continue;
			cl_vtx_t *corner = 0;
			size_t n = 0, cap = 0;
			size_t p = b + dlo;
			const size_t end = p + dls;
			while (p + 3 <= end && d[p] >= 0x80)
			{
				const uint cmd = d[p], cnt = cl16 (d + p + 1);
				p += 3;
				if ((size_t)cnt * vsz > end - p)
					break;
				cl_vtx_t *v = malloc ((cnt ? cnt : 1) * sizeof (*v));
				if (!v)
					break;
				for (uint i = 0; i < cnt; i++)
				{
					const u8 *q = d + p + (size_t)i * vsz + ((fmt & 3) ? 1 : 0);
					uint o = 0, val[8];
					for (uint a = 0; a < 8; a++)
					{
						val[a] = w[a] == 1 ? q[o] : w[a] == 2 ? cl16 (q + o) : 0;
						o += w[a];
					}
					v[i] = (cl_vtx_t) { val[0], val[1], val[4] };
				}
				p += (size_t)cnt * vsz;
				uint tri[6 * 4096];
				uint nt = 0;
				for (uint i = 0; i + 2 < cnt && nt + 6 <= sizeof (tri) / sizeof (*tri); i++)
				{
					uint a, bb, c;
					if (cmd == 0x90)
					{
						if (i % 3)
							continue;
						a = i, bb = i + 1, c = i + 2;
					}
					else if (cmd == 0x98)
						a = i & 1 ? i + 1 : i, bb = i & 1 ? i : i + 1, c = i + 2;
					else if (cmd == 0xa0)
						a = 0, bb = i + 1, c = i + 2;
					else
						break;
					if (v[a].pos == v[bb].pos || v[bb].pos == v[c].pos || v[a].pos == v[c].pos)
						continue;
					tri[nt++] = a, tri[nt++] = bb, tri[nt++] = c;
				}
				if (cmd == 0x80)
					for (uint i = 0; i + 3 < cnt && nt + 6 <= sizeof (tri) / sizeof (*tri); i += 4)
					{
						const uint quad[6] = { i, i + 1, i + 2, i, i + 2, i + 3 };
						for (uint j = 0; j < 6; j++)
							tri[nt++] = quad[j];
					}
				for (uint j = 0; j < nt; j++)
				{
					if (n == cap)
					{
						cap = cap ? cap * 2 : 1024;
						cl_vtx_t *nc = realloc (corner, cap * sizeof (*corner));
						if (!nc)
							break;
						corner = nc;
					}
					corner[n++] = v[tri[j]];
				}
				free (v);
				if (cmd != 0x90 && cmd != 0x98 && cmd != 0xa0 && cmd != 0x80)
					break;
			}
			n -= n % 3;
			cl_flush (m, d, &pa, &na, &ta, mat, corner, n, meshno++);
			free (corner);
		}
	}
	if (!m->num_meshes)
	{
		FreeModel (m);
		return 0;
	}
	return m;
}
