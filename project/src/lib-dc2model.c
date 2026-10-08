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

model_t *ParseDC2Model (const u8 *d, size_t size)
{
	if (!IsDC2Model (d, size))
		return 0;

	// locate the {position, normal, texcoord} block triple
	size_t pp = 0, pn = 0, pu = 0, prim = 0;
	uint np = 0, nn = 0, nu = 0, sets = 0;
	for (size_t p = 0x30; p + 9 < size;)
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
				if (!sets++)
				{
					pp = p;
					np = a;
					pn = q;
					nn = b;
					pu = r;
					nu = c;
					prim = r + 9 + 8 * (size_t)c;
				}
				p = r + 9 + 8 * (size_t)c;
				continue;
			}
		}
		p++;
	}
	if (sets != 1)
		return 0;

	// strip records: find the first validated 0x9b record shortly after the arrays
	size_t s = prim;
	for (; s < size && s < prim + 64; s++)
		if (d[s] == 0x9b)
			break;
	if (s >= size || s >= prim + 64)
		return 0;

	model_t *model = CALLOC (1, sizeof (*model));
	mesh_t *mesh = CALLOC (1, sizeof (*mesh));
	material_t *mat = CALLOC (1, sizeof (*mat));
	if (!model || !mesh || !mat)
	{
		FREE (model);
		FREE (mesh);
		FREE (mat);
		return 0;
	}
	mesh->positions = CALLOC (np, sizeof (vec3_t));
	mesh->normals = CALLOC (nn, sizeof (vec3_t));
	mesh->texcoords = CALLOC (nu, sizeof (vec2_t));
	if (!mesh->positions || !mesh->normals || !mesh->texcoords)
	{
		FREE (mesh->positions);
		FREE (mesh->normals);
		FREE (mesh->texcoords);
		FREE (model);
		FREE (mesh);
		FREE (mat);
		return 0;
	}
	for (uint i = 0; i < np; i++)
	{
		const u8 *v = d + pp + 9 + 12 * (size_t)i;
		mesh->positions[i].x = dc2_rdf (v);
		mesh->positions[i].y = dc2_rdf (v + 4);
		mesh->positions[i].z = dc2_rdf (v + 8);
	}
	for (uint i = 0; i < nn; i++)
	{
		const u8 *v = d + pn + 9 + 12 * (size_t)i;
		mesh->normals[i].x = dc2_rdf (v);
		mesh->normals[i].y = dc2_rdf (v + 4);
		mesh->normals[i].z = dc2_rdf (v + 8);
	}
	for (uint i = 0; i < nu; i++)
	{
		const u8 *v = d + pu + 9 + 8 * (size_t)i;
		mesh->texcoords[i].u = dc2_rdf (v);
		mesh->texcoords[i].v = dc2_rdf (v + 4);
	}
	mesh->num_positions = np;
	mesh->num_normals = nn;
	mesh->num_texcoords = nu;

	size_t cap = 0, num = 0;
	vertex_t *verts = 0;
	int *tri_mat = 0;
	bool ok = true;
	while (ok && s + 3 <= size && d[s] == 0x9b)
	{
		const uint n = rd_be16 (d + s + 1);
		if (s + 3 + 6 * (size_t)n > size)
			break;
		const u8 *vp = d + s + 3;
		bool good = true;
		for (uint i = 0; i < n && good; i++)
			good = rd_be16 (vp + 6 * i) < np && rd_be16 (vp + 6 * i + 2) < nn
				&& rd_be16 (vp + 6 * i + 4) < nu;
		if (!good)
			break;
		for (uint t = 0; t + 2 < n && ok; t++)
		{
			const uint i3[3] = { t & 1 ? t + 1 : t, t & 1 ? t : t + 1, t + 2 };
			if (num + 3 > cap)
			{
				cap = cap ? cap * 2 : 768;
				vertex_t *nv = REALLOC (verts, cap * sizeof (*nv));
				int *nt = REALLOC (tri_mat, (cap / 3 + 1) * sizeof (*nt));
				if (nv)
					verts = nv;
				if (nt)
					tri_mat = nt;
				if (!nv || !nt)
				{
					ok = false;
					break;
				}
			}
			for (uint k = 0; k < 3; k++)
			{
				const u8 *e = vp + 6 * i3[k];
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
		FREE (mesh->positions);
		FREE (mesh->normals);
		FREE (mesh->texcoords);
		FREE (verts);
		FREE (tri_mat);
		FREE (model);
		FREE (mesh);
		FREE (mat);
		return 0;
	}
	snprintf (mesh->name, sizeof (mesh->name), "mesh");
	mesh->vertices = verts;
	mesh->num_vertices = num;
	mesh->triangle_materials = tri_mat;
	mesh->material_idx = 0;
	snprintf (mat->name, sizeof (mat->name), "material");
	mat->diffuse[0] = mat->diffuse[1] = mat->diffuse[2] = mat->diffuse[3] = 1.0f;
	model->meshes = mesh;
	model->num_meshes = 1;
	model->materials = mat;
	model->num_materials = 1;
	return model;
}
