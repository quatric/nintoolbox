// SPDX-License-Identifier: GPL-2.0+
#include "lib-bnfm.h"
#include "lib-nintendo.h"
#include "lib-std.h"
#include <string.h>
#include <math.h>

static float bnfm_rd_f16 (u16 h)
{
	const int sign = (h >> 15) & 1;
	int exp = (h >> 10) & 0x1F;
	int man = h & 0x3FF;
	float v;
	if (!exp)
		v = man ? (float)man / 16384.0f / 64.0f : 0.0f;
	else if (exp == 31)
		v = man ? 0.0f : 1e30f;
	else
	{
		float m = 1.0f + (float)man / 1024.0f;
		int e = exp - 15;
		v = m;
		while (e > 0) { v *= 2.0f; e--; }
		while (e < 0) { v /= 2.0f; e++; }
	}
	return sign ? -v : v;
}

static float bnfm_bef32 (const u8 *p)
{
	union { u32 u; float f; } c;
	c.u = rd_be32 (p);
	return c.f;
}

static ccp bnfm_get_str (const u8 *data, uint size, u32 offset)
{
	if (!offset || offset >= size)
		return "";
	return (ccp)(data + offset);
}

enumError DecodeBNFM (const u8 *data, uint size, ccp out_path)
{
	if (!data || size < 0x78)
		return ERR_INVALID_DATA;

	const u32 face_offset = rd_be32 (data + 0x0C);
	const u32 face_length = rd_be32 (data + 0x10);
	const u32 vertex_length = rd_be32 (data + 0x14);
	const u32 vert_offset = rd_be32 (data + 0x20);
	const u32 bone_count = rd_be32 (data + 0x30);
	const u32 poly_count = rd_be32 (data + 0x34);
	const u32 mat_length = rd_be32 (data + 0x38);
	const u32 bone_offset = rd_be32 (data + 0x58);
	const u32 poly_info_offset = rd_be32 (data + 0x5C);
	const u32 material_offset = rd_be32 (data + 0x60);

	if (face_offset + face_length > size || vert_offset + vertex_length > size)
		return ERR_INVALID_DATA;

	const uint num_indices = face_length / 2;
	const uint num_total_verts = vertex_length / 44;

	u16 *indices = MALLOC (num_indices * sizeof (u16));
	if (!indices)
		return ERR_CANT_CREATE;
	for (uint i = 0; i < num_indices; i++)
		indices[i] = rd_be16 (data + face_offset + i * 2);

	model_t model;
	memset (&model, 0, sizeof (model));

	// --- Joints / Skeleton ---
	if (bone_count > 0 && bone_offset < size)
	{
		model.num_joints = bone_count;
		model.joints = CALLOC (bone_count, sizeof (joint_t));
		for (uint i = 0; i < bone_count; i++)
		{
			const u32 boff = bone_offset + i * 0xB0;
			if (boff + 0x68 > size)
				break;
			joint_t *j = model.joints + i;
			const u32 name_off = rd_be32 (data + boff);
			const u32 parent_name_off = rd_be32 (data + boff + 8);
			ccp bname = bnfm_get_str (data, size, name_off);
			ccp pname = bnfm_get_str (data, size, parent_name_off);
			snprintf (j->name, sizeof (j->name), "%s", bname[0] ? bname : "bone");
			j->parent_idx = -1;
			if (pname[0])
			{
				for (uint k = 0; k < i; k++)
				{
					if (!strcmp (model.joints[k].name, pname))
					{
						j->parent_idx = (int)k;
						break;
					}
				}
			}
			j->translate.x = bnfm_bef32 (data + boff + 0x20);
			j->translate.y = bnfm_bef32 (data + boff + 0x24);
			j->translate.z = bnfm_bef32 (data + boff + 0x28);
			j->scale.x = bnfm_bef32 (data + boff + 0x2C);
			j->scale.y = bnfm_bef32 (data + boff + 0x30);
			j->scale.z = bnfm_bef32 (data + boff + 0x34);
			if (j->scale.x == 0.0f) j->scale.x = 1.0f;
			if (j->scale.y == 0.0f) j->scale.y = 1.0f;
			if (j->scale.z == 0.0f) j->scale.z = 1.0f;

			// Inverse bind matrix (3x4 affine)
			if (boff + 0x88 <= size)
			{
				for (int m = 0; m < 12; m++)
					j->inverse_bind[m] = bnfm_bef32 (data + boff + 0x48 + m * 4);
				j->has_inverse_bind = 1;
			}
		}
	}

	// --- Materials ---
	const uint num_materials = mat_length > 0 ? (mat_length / 6 > 0 ? mat_length / 6 : 1) : 1;
	model.num_materials = num_materials;
	model.materials = CALLOC (num_materials, sizeof (material_t));
	for (uint i = 0; i < num_materials; i++)
	{
		material_t *mat = model.materials + i;
		snprintf (mat->name, sizeof (mat->name), "mat_%u", i);
		mat->diffuse[0] = 1.0f;
		mat->diffuse[1] = 1.0f;
		mat->diffuse[2] = 1.0f;
		mat->diffuse[3] = 1.0f;

		const u32 moff = material_offset + i * 0x228;
		if (moff + 0x118 <= size)
		{
			const u32 mname_off = rd_be32 (data + moff);
			const u32 tname_off = rd_be32 (data + moff + 0x114);
			ccp mname = bnfm_get_str (data, size, mname_off);
			ccp tname = bnfm_get_str (data, size, tname_off);
			if (mname[0])
				snprintf (mat->name, sizeof (mat->name), "%s", mname);
			if (tname[0])
			{
				snprintf (mat->textures[0], sizeof (mat->textures[0]), "%s", tname);
				mat->num_textures = 1;
			}
		}
	}

	// --- Meshes / Polygons ---
	model.num_meshes = poly_count > 0 ? poly_count : 1;
	model.meshes = CALLOC (model.num_meshes, sizeof (mesh_t));

	uint curr_idx_offset = 0;
	uint curr_vert_offset = 0;

	for (uint i = 0; i < model.num_meshes; i++)
	{
		mesh_t *mesh = model.meshes + i;
		snprintf (mesh->name, sizeof (mesh->name), "mesh_%u", i);

		uint index_count = 0;
		uint vert_count = 0;
		uint mat_id = 0;

		if (poly_info_offset > 0 && poly_info_offset + i * 0x30 + 0x28 <= size)
		{
			const u32 poff = poly_info_offset + i * 0x30;
			const u32 name_off = rd_be32 (data + poff);
			ccp pname = bnfm_get_str (data, size, name_off);
			if (pname[0])
				snprintf (mesh->name, sizeof (mesh->name), "%s", pname);
			index_count = rd_be32 (data + poff + 0x18);
			vert_count = rd_be32 (data + poff + 0x1C);
			mat_id = rd_be32 (data + poff + 0x24);
		}
		else
		{
			index_count = num_indices;
			vert_count = num_total_verts;
		}

		if (mat_id < model.num_materials)
			mesh->material_idx = (int)mat_id;

		if (curr_vert_offset + vert_count > num_total_verts)
			vert_count = num_total_verts > curr_vert_offset ? num_total_verts - curr_vert_offset : 0;
		if (curr_idx_offset + index_count > num_indices)
			index_count = num_indices > curr_idx_offset ? num_indices - curr_idx_offset : 0;

		if (vert_count > 0)
		{
			mesh->num_positions = vert_count;
			mesh->positions = MALLOC (vert_count * sizeof (vec3_t));
			mesh->num_normals = vert_count;
			mesh->normals = MALLOC (vert_count * sizeof (vec3_t));
			mesh->num_texcoords = vert_count;
			mesh->texcoords = MALLOC (vert_count * sizeof (vec2_t));
			mesh->num_colors[0] = vert_count;
			mesh->colors[0] = MALLOC (vert_count * sizeof (color4_t));

			for (uint v = 0; v < vert_count; v++)
			{
				const u32 voff = vert_offset + (curr_vert_offset + v) * 44;
				if (voff + 44 > size)
					break;
				const u8 *vp = data + voff;
				mesh->positions[v].x = bnfm_bef32 (vp);
				mesh->positions[v].y = bnfm_bef32 (vp + 4);
				mesh->positions[v].z = bnfm_bef32 (vp + 8);

				mesh->normals[v].x = (float)(int8_t)vp[12] / 127.0f;
				mesh->normals[v].y = (float)(int8_t)vp[13] / 127.0f;
				mesh->normals[v].z = (float)(int8_t)vp[14] / 127.0f;

				mesh->colors[0][v].r = (float)vp[16] / 255.0f;
				mesh->colors[0][v].g = (float)vp[17] / 255.0f;
				mesh->colors[0][v].b = (float)vp[18] / 255.0f;
				mesh->colors[0][v].a = (float)vp[19] / 255.0f;

				mesh->texcoords[v].u = bnfm_rd_f16 (rd_be16 (vp + 20));
				mesh->texcoords[v].v = bnfm_rd_f16 (rd_be16 (vp + 22));
			}
		}

		if (index_count > 0)
		{
			mesh->num_vertices = index_count;
			mesh->vertices = MALLOC (index_count * sizeof (vertex_t));
			for (uint j = 0; j < index_count; j++)
			{
				const u16 raw_idx = indices[curr_idx_offset + j];
				const int local_idx = raw_idx < vert_count ? (int)raw_idx : 0;
				mesh->vertices[j].position_idx = local_idx;
				mesh->vertices[j].normal_idx = local_idx;
				mesh->vertices[j].tangent_idx = -1;
				mesh->vertices[j].texcoord_idx = local_idx;
				mesh->vertices[j].matrix_idx = -1;
				mesh->vertices[j].color_idx[0] = local_idx;
				mesh->vertices[j].color_idx[1] = -1;
				for (int e = 0; e < 7; e++)
					mesh->vertices[j].extra_texcoord_idx[e] = -1;
			}
		}

		curr_idx_offset += index_count;
		curr_vert_offset += vert_count;
	}

	FREE (indices);

	const int rc = ExportModelToGLB (&model, out_path);

	// Free resources
	for (uint i = 0; i < model.num_meshes; i++)
	{
		mesh_t *m = model.meshes + i;
		FREE (m->positions);
		FREE (m->normals);
		FREE (m->texcoords);
		FREE (m->colors[0]);
		FREE (m->vertices);
	}
	FREE (model.meshes);
	FREE (model.joints);
	FREE (model.materials);

	return rc == 0 ? ERR_OK : ERR_CANT_CREATE;
}
