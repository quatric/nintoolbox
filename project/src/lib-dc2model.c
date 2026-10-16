// SPDX-License-Identifier: GPL-2.0+
// "DC2" container models -- see lib-dc2model.h for the layout.

#include "lib-dc2model.h"
#include "lib-nintendo.h"
#include <string.h>
#include <math.h>

bool IsDC2Model (const u8 *data, size_t size)
{
	return data && size > 0x60 && !memcmp (data, "DC2\0", 4);
}

static float dc2_rdf (const u8 *p)
{
	const u32 v = rd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return isfinite (f) ? f : 0.0f;
}

// block header at P; returns the element count or 0 when it does not match
static uint dc2_block (const u8 *d, size_t size, size_t p, uint stride)
{
	if (p + 9 > size || rd_be32 (d + p) || d[p + 4] != stride)
		return 0;
	const u32 n = rd_be32 (d + p + 5);
	if (!n || n > 0x100000 || p + 9 + (u64)n * stride > size)
		return 0;
	return n;
}

#define DC2_MAX_SETS 256

typedef struct dc2_set_t
{
	size_t pp, pn, pu;
	uint np, nn, nu;
} dc2_set_t;

// GX primitive records (vertex format 3): 0x83 quads, 0x93 triangles, 0x9b triangle strip, 0xa3 triangle fan
static bool dc2_is_prim (u8 op)
{
	return op == 0x83 || op == 0x93 || op == 0x9b || op == 0xa3;
}

static void dc2_free_mesh (mesh_t *mesh)
{
	FREE (mesh->positions);
	FREE (mesh->normals);
	FREE (mesh->texcoords);
	FREE (mesh->vertices);
	FREE (mesh->triangle_materials);
}

// Parts are stored as {position, normal, texcoord} array triples one after the other, followed by one run of 0x9b triangle
// strip records per part, in the same order. Records hold position / normal / texcoord indices into their part's arrays.
model_t *ParseDC2Model (const u8 *d, size_t size)
{
	if (!IsDC2Model (d, size))
		return 0;

	dc2_set_t *sets = CALLOC (DC2_MAX_SETS, sizeof (*sets));
	uint nsets = 0;
	size_t prim = 0;
	if (!sets)
		return 0;
	for (size_t p = 0x30; p + 9 < size && nsets < DC2_MAX_SETS;)
	{
		const uint a = dc2_block (d, size, p, 12);
		if (a)
		{
			const size_t q = p + 9 + 12 * (size_t)a;
			const uint b = dc2_block (d, size, q, 12);
			const size_t r = q + 9 + 12 * (size_t)b;
			const uint c = b ? dc2_block (d, size, r, 8) : 0;
			if (c)
			{
				dc2_set_t *st = sets + nsets++;
				st->pp = p;
				st->np = a;
				st->pn = q;
				st->nn = b;
				st->pu = r;
				st->nu = c;
				prim = p = r + 9 + 8 * (size_t)c;
				continue;
			}
		}
		p++;
	}
	if (!nsets)
	{
		FREE (sets);
		return 0;
	}

	model_t *model = CALLOC (1, sizeof (*model));
	mesh_t *meshes = CALLOC (nsets, sizeof (*meshes));
	material_t *mat = CALLOC (1, sizeof (*mat));
	if (!model || !meshes || !mat)
	{
		FREE (model);
		FREE (meshes);
		FREE (mat);
		FREE (sets);
		return 0;
	}

	uint nm = 0;
	size_t s = prim;
	for (uint k = 0; k < nsets; k++)
	{
		const dc2_set_t *st = sets + k;
		// strip records: first 0x9b shortly after the previous run
		size_t lim = s + 64 < size ? s + 64 : size;
		for (; s < lim && d[s] != 0x9b && d[s] != 0x93; s++)
			;
		if (s >= lim)
			continue;
		mesh_t *mesh = meshes + nm;
		size_t cap = 0, num = 0;
		vertex_t *verts = 0;
		int *tri_mat = 0;
		bool ok = true;
		while (ok && s + 3 <= size && dc2_is_prim (d[s]))
		{
			const uint n = rd_be16 (d + s + 1);
			if (s + 3 + 6 * (size_t)n > size)
				break;
			const u8 *vp = d + s + 3;
			bool good = true;
			for (uint i = 0; i < n && good; i++)
				good = rd_be16 (vp + 6 * i) < st->np && rd_be16 (vp + 6 * i + 2) < st->nn
					&& rd_be16 (vp + 6 * i + 4) < st->nu;
			if (!good)
				break;
			const u8 op = d[s];
			const uint ntri = op == 0x93 ? n / 3 : op == 0x83 ? n / 4 * 2 : n >= 3 ? n - 2 : 0;
			for (uint t = 0; t < ntri && ok; t++)
			{
				uint i3[3];
				if (op == 0x93)
					i3[0] = 3 * t, i3[1] = 3 * t + 1, i3[2] = 3 * t + 2;
				else if (op == 0x83)
				{
					const uint q = 4 * (t / 2);
					if (t & 1)
						i3[0] = q, i3[1] = q + 2, i3[2] = q + 3;
					else
						i3[0] = q, i3[1] = q + 1, i3[2] = q + 2;
				}
				else if (op == 0xa3)
					i3[0] = 0, i3[1] = t + 1, i3[2] = t + 2;
				else
					i3[0] = t & 1 ? t + 1 : t, i3[1] = t & 1 ? t : t + 1, i3[2] = t + 2;
				if (num + 3 > cap)
				{
					cap = cap ? cap * 2 : 768;
					vertex_t *nv = REALLOC (verts, cap * sizeof (*nv));
					if (nv)
						verts = nv;
					int *nt = REALLOC (tri_mat, (cap / 3 + 1) * sizeof (*nt));
					if (nt)
						tri_mat = nt;
					if (!nv || !nt)
					{
						ok = false;
						break;
					}
				}
				for (uint j = 0; j < 3; j++)
				{
					const u8 *e = vp + 6 * i3[j];
					vertex_t *v = verts + num++;
					memset (v, 0, sizeof (*v));
					v->position_idx = rd_be16 (e);
					v->normal_idx = rd_be16 (e + 2);
					v->texcoord_idx = rd_be16 (e + 4);
					v->tangent_idx = v->matrix_idx = -1;
					v->color_idx[0] = v->color_idx[1] = -1;
					for (int x = 0; x < 7; x++)
						v->extra_texcoord_idx[x] = -1;
				}
				tri_mat[num / 3 - 1] = 0;
			}
			s += 3 + 6 * (size_t)n;
		}
		if (!ok || !num)
		{
			FREE (verts);
			FREE (tri_mat);
			continue;
		}
		mesh->positions = CALLOC (st->np, sizeof (vec3_t));
		mesh->normals = CALLOC (st->nn, sizeof (vec3_t));
		mesh->texcoords = CALLOC (st->nu, sizeof (vec2_t));
		if (!mesh->positions || !mesh->normals || !mesh->texcoords)
		{
			dc2_free_mesh (mesh);
			FREE (verts);
			FREE (tri_mat);
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}
		for (uint i = 0; i < st->np; i++)
		{
			const u8 *v = d + st->pp + 9 + 12 * (size_t)i;
			mesh->positions[i].x = dc2_rdf (v);
			mesh->positions[i].y = dc2_rdf (v + 4);
			mesh->positions[i].z = dc2_rdf (v + 8);
		}
		for (uint i = 0; i < st->nn; i++)
		{
			const u8 *v = d + st->pn + 9 + 12 * (size_t)i;
			mesh->normals[i].x = dc2_rdf (v);
			mesh->normals[i].y = dc2_rdf (v + 4);
			mesh->normals[i].z = dc2_rdf (v + 8);
		}
		for (uint i = 0; i < st->nu; i++)
		{
			const u8 *v = d + st->pu + 9 + 8 * (size_t)i;
			mesh->texcoords[i].u = dc2_rdf (v);
			mesh->texcoords[i].v = dc2_rdf (v + 4);
		}
		mesh->num_positions = st->np;
		mesh->num_normals = st->nn;
		mesh->num_texcoords = st->nu;
		snprintf (mesh->name, sizeof (mesh->name), "mesh%u", k);
		mesh->vertices = verts;
		mesh->num_vertices = num;
		mesh->triangle_materials = tri_mat;
		mesh->material_idx = 0;
		nm++;
	}
	FREE (sets);
	if (!nm)
	{
		FREE (model);
		FREE (meshes);
		FREE (mat);
		return 0;
	}
	snprintf (mat->name, sizeof (mat->name), "material");
	mat->diffuse[0] = mat->diffuse[1] = mat->diffuse[2] = mat->diffuse[3] = 1.0f;
	model->meshes = meshes;
	model->num_meshes = nm;
	model->materials = mat;
	model->num_materials = 1;
	return model;
}
