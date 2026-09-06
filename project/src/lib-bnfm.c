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

static u16 bnfm_wr_f16 (float f)
{
	union { u32 u; float f; } c;
	c.f = f;
	u32 x = c.u;
	u32 sign = (x >> 31) & 1;
	int32_t exp = ((x >> 23) & 0xFF) - 127 + 15;
	u32 mant = (x >> 13) & 0x3FF;

	if (exp <= 0)
		return (u16)(sign << 15);
	if (exp >= 31)
		return (u16)((sign << 15) | 0x7C00);
	return (u16)((sign << 15) | (exp << 10) | mant);
}

static float bnfm_bef32 (const u8 *p)
{
	union { u32 u; float f; } c;
	c.u = rd_be32 (p);
	return c.f;
}

static void bnfm_wr_bef32 (u8 *p, float f)
{
	union { u32 u; float f; } c;
	c.f = f;
	wr_be32 (p, c.u);
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

enumError EncodeModelToBNFM (const model_t *model, ccp out_path)
{
	if (!model || !model->num_meshes || !out_path)
		return ERR_INVALID_DATA;

	uint total_verts = 0;
	uint total_indices = 0;
	for (uint i = 0; i < model->num_meshes; i++)
	{
		total_verts += model->meshes[i].num_positions;
		total_indices += model->meshes[i].num_vertices;
	}

	if (!total_verts || !total_indices)
		return ERR_INVALID_DATA;

	const u32 bone_count = model->num_joints;
	const u32 poly_count = model->num_meshes;
	const u32 mat_count = model->num_materials > 0 ? model->num_materials : 1;

	// Calculate layout
	const u32 hdr_size = 0x80;
	const u32 poly_info_offset = hdr_size;
	const u32 poly_info_size = poly_count * 0x30;

	const u32 bone_offset = poly_info_offset + poly_info_size;
	const u32 bone_size = bone_count * 0xB0;

	const u32 material_offset = bone_offset + bone_size;
	const u32 material_size = mat_count * 0x228;

	// String table
	u32 strtab_size = 32; // initial space for default names
	for (uint i = 0; i < poly_count; i++)
		strtab_size += strlen (model->meshes[i].name) + 1;
	for (uint i = 0; i < bone_count; i++)
		strtab_size += strlen (model->joints[i].name) + 1;
	for (uint i = 0; i < mat_count; i++)
	{
		if (model->materials)
		{
			strtab_size += strlen (model->materials[i].name) + 1;
			if (model->materials[i].num_textures > 0)
				strtab_size += strlen (model->materials[i].textures[0]) + 1;
		}
	}
	strtab_size = (strtab_size + 0x1F) & ~0x1Fu;

	const u32 strtab_offset = material_offset + material_size;
	const u32 face_offset = (strtab_offset + strtab_size + 0x1F) & ~0x1Fu;
	const u32 face_length = (total_indices * 2 + 0x1F) & ~0x1Fu;

	const u32 vert_offset = face_offset + face_length;
	const u32 vertex_length = (total_verts * 44 + 0x1F) & ~0x1Fu;

	const u32 total_file_size = vert_offset + vertex_length;

	u8 *out = CALLOC (1, total_file_size);
	if (!out)
		return ERR_OUT_OF_MEMORY;

	// BNFM Header
	memcpy (out, "BNFM", 4);
	wr_be32 (out + 0x08, 0x00010000);
	wr_be32 (out + 0x0C, face_offset);
	wr_be32 (out + 0x10, face_length);
	wr_be32 (out + 0x14, vertex_length);
	wr_be32 (out + 0x20, vert_offset);
	wr_be32 (out + 0x30, bone_count);
	wr_be32 (out + 0x34, poly_count);
	wr_be32 (out + 0x38, mat_count * 6);
	wr_be32 (out + 0x58, bone_offset);
	wr_be32 (out + 0x5C, poly_info_offset);
	wr_be32 (out + 0x60, material_offset);

	// String table writer
	u32 cur_str = strtab_offset;

	// Bones
	for (uint i = 0; i < bone_count; i++)
	{
		const u32 boff = bone_offset + i * 0xB0;
		const joint_t *j = model->joints + i;

		wr_be32 (out + boff, cur_str);
		const size_t nlen = strlen (j->name);
		memcpy (out + cur_str, j->name, nlen + 1);
		cur_str += nlen + 1;

		if (j->parent_idx >= 0 && (uint)j->parent_idx < bone_count)
		{
			wr_be32 (out + boff + 8, cur_str);
			const size_t plen = strlen (model->joints[j->parent_idx].name);
			memcpy (out + cur_str, model->joints[j->parent_idx].name, plen + 1);
			cur_str += plen + 1;
		}

		bnfm_wr_bef32 (out + boff + 0x20, j->translate.x);
		bnfm_wr_bef32 (out + boff + 0x24, j->translate.y);
		bnfm_wr_bef32 (out + boff + 0x28, j->translate.z);
		bnfm_wr_bef32 (out + boff + 0x2C, j->scale.x != 0.0f ? j->scale.x : 1.0f);
		bnfm_wr_bef32 (out + boff + 0x30, j->scale.y != 0.0f ? j->scale.y : 1.0f);
		bnfm_wr_bef32 (out + boff + 0x34, j->scale.z != 0.0f ? j->scale.z : 1.0f);

		if (j->has_inverse_bind)
		{
			for (int m = 0; m < 12; m++)
				bnfm_wr_bef32 (out + boff + 0x48 + m * 4, j->inverse_bind[m]);
		}
		else
		{
			// Identity 3x4 affine
			bnfm_wr_bef32 (out + boff + 0x48, 1.0f);
			bnfm_wr_bef32 (out + boff + 0x58, 1.0f);
			bnfm_wr_bef32 (out + boff + 0x68, 1.0f);
		}
	}

	// Materials
	for (uint i = 0; i < mat_count; i++)
	{
		const u32 moff = material_offset + i * 0x228;
		ccp mname = (model->materials && model->materials[i].name[0]) ? model->materials[i].name : "material";
		wr_be32 (out + moff, cur_str);
		const size_t mnlen = strlen (mname);
		memcpy (out + cur_str, mname, mnlen + 1);
		cur_str += mnlen + 1;

		if (model->materials && model->materials[i].num_textures > 0 && model->materials[i].textures[0][0])
		{
			ccp tname = model->materials[i].textures[0];
			wr_be32 (out + moff + 0x114, cur_str);
			const size_t tnlen = strlen (tname);
			memcpy (out + cur_str, tname, tnlen + 1);
			cur_str += tnlen + 1;
		}
	}

	// Meshes & Polys
	uint vert_cursor = 0;
	uint idx_cursor = 0;

	for (uint i = 0; i < poly_count; i++)
	{
		const mesh_t *mesh = model->meshes + i;
		const u32 poff = poly_info_offset + i * 0x30;

		wr_be32 (out + poff, cur_str);
		const size_t plen = strlen (mesh->name);
		memcpy (out + cur_str, mesh->name, plen + 1);
		cur_str += plen + 1;

		wr_be32 (out + poff + 0x18, (u32)mesh->num_vertices);
		wr_be32 (out + poff + 0x1C, (u32)mesh->num_positions);
		wr_be32 (out + poff + 0x24, (u32)(mesh->material_idx >= 0 ? mesh->material_idx : 0));

		// Indices
		for (uint j = 0; j < mesh->num_vertices; j++)
		{
			u16 idx = (u16)mesh->vertices[j].position_idx;
			wr_be16 (out + face_offset + (idx_cursor + j) * 2, idx);
		}
		idx_cursor += mesh->num_vertices;

		// Vertices (44 bytes per vertex)
		for (uint v = 0; v < mesh->num_positions; v++)
		{
			u8 *vp = out + vert_offset + (vert_cursor + v) * 44;
			bnfm_wr_bef32 (vp, mesh->positions[v].x);
			bnfm_wr_bef32 (vp + 4, mesh->positions[v].y);
			bnfm_wr_bef32 (vp + 8, mesh->positions[v].z);

			if (mesh->normals)
			{
				vp[12] = (u8)(int8_t)(mesh->normals[v].x * 127.0f);
				vp[13] = (u8)(int8_t)(mesh->normals[v].y * 127.0f);
				vp[14] = (u8)(int8_t)(mesh->normals[v].z * 127.0f);
			}
			vp[15] = 0x7F;

			if (mesh->colors[0])
			{
				vp[16] = (u8)(mesh->colors[0][v].r * 255.0f);
				vp[17] = (u8)(mesh->colors[0][v].g * 255.0f);
				vp[18] = (u8)(mesh->colors[0][v].b * 255.0f);
				vp[19] = (u8)(mesh->colors[0][v].a * 255.0f);
			}
			else
			{
				vp[16] = 0xFF; vp[17] = 0xFF; vp[18] = 0xFF; vp[19] = 0xFF;
			}

			if (mesh->texcoords)
			{
				wr_be16 (vp + 20, bnfm_wr_f16 (mesh->texcoords[v].u));
				wr_be16 (vp + 22, bnfm_wr_f16 (mesh->texcoords[v].v));
			}
		}
		vert_cursor += mesh->num_positions;
	}

	File_t F;
	enumError err = CreateFileOpt (&F, true, out_path, false, out_path);
	if (!err && F.f && fwrite (out, 1, total_file_size, F.f) != total_file_size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing BNFM failed: %s\n", out_path);
	ResetFile (&F, opt_preserve);

	FREE (out);
	return err;
}

