#include "lib-std.h"
#include "lib-nud.h"
#include "lib-brres-model.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#undef calloc
#undef malloc
#undef realloc
#undef free

static inline uint16_t rd_be16 (const uint8_t *p)
{
	return (uint16_t)p[0] << 8 | p[1];
}
static inline uint32_t rd_be32 (const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static inline uint16_t rd_le16 (const uint8_t *p)
{
	return (uint16_t)p[1] << 8 | p[0];
}
static inline uint32_t rd_le32 (const uint8_t *p)
{
	return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

static inline void wr_be16 (uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}
static inline void wr_be32 (uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static float rd_f32 (const uint8_t *p, bool is_be)
{
	union
	{
		uint32_t u;
		float f;
	} c;
	c.u = is_be ? rd_be32 (p) : rd_le32 (p);
	return c.f;
}

static void wr_f32 (uint8_t *p, float f, bool is_be)
{
	union
	{
		uint32_t u;
		float f;
	} c;
	c.f = f;
	if (is_be)
		wr_be32 (p, c.u);
	else
	{
		p[0] = (uint8_t)c.u;
		p[1] = (uint8_t)(c.u >> 8);
		p[2] = (uint8_t)(c.u >> 16);
		p[3] = (uint8_t)(c.u >> 24);
	}
}

static float rd_f16 (uint16_t h)
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
		while (e > 0)
		{
			v *= 2.0f;
			e--;
		}
		while (e < 0)
		{
			v /= 2.0f;
			e++;
		}
	}
	return sign ? -v : v;
}

static uint16_t wr_f16 (float f)
{
	union
	{
		uint32_t u;
		float f;
	} c;
	c.f = f;
	uint32_t x = c.u;
	uint32_t sign = (x >> 31) & 1;
	int32_t exp = ((x >> 23) & 0xFF) - 127 + 15;
	uint32_t mant = (x >> 13) & 0x3FF;

	if (exp <= 0)
		return (uint16_t)(sign << 15);
	if (exp >= 31)
		return (uint16_t)((sign << 15) | 0x7C00);
	return (uint16_t)((sign << 15) | (exp << 10) | mant);
}

bool IsNUD (const uint8_t *data, size_t size)
{
	if (!data || size < 16)
		return false;
	return !memcmp (data, "NDP3", 4) || !memcmp (data, "NDWU", 4) || !memcmp (data, "NDWD", 4);
}

model_t *ParseNUD (const uint8_t *data, size_t size)
{
	if (!IsNUD (data, size) || size < 0x30)
		return NULL;

	const bool is_be = memcmp (data, "NDWD", 4) != 0;
	const uint16_t polysets = is_be ? rd_be16 (data + 0x0a) : rd_le16 (data + 0x0a);
	const int32_t poly_clump_rel
		= is_be ? (int32_t)rd_be32 (data + 0x10) : (int32_t)rd_le32 (data + 0x10);
	const int32_t poly_clump_sz
		= is_be ? (int32_t)rd_be32 (data + 0x14) : (int32_t)rd_le32 (data + 0x14);
	const int32_t vert_clump_sz
		= is_be ? (int32_t)rd_be32 (data + 0x18) : (int32_t)rd_le32 (data + 0x18);
	const int32_t vertadd_clump_sz
		= is_be ? (int32_t)rd_be32 (data + 0x1c) : (int32_t)rd_le32 (data + 0x1c);

	const size_t poly_clump_start = 0x30 + (size_t)(poly_clump_rel > 0 ? poly_clump_rel : 0);
	const size_t vert_clump_start = poly_clump_start + (size_t)(poly_clump_sz > 0 ? poly_clump_sz : 0);
	const size_t vertadd_clump_start
		= vert_clump_start + (size_t)(vert_clump_sz > 0 ? vert_clump_sz : 0);
	const size_t name_start
		= vertadd_clump_start + (size_t)(vertadd_clump_sz > 0 ? vertadd_clump_sz : 0);

	// Try full Smash 4 / Pokkén NUD format with polysets and poly descriptors
	if (polysets > 0 && polysets <= 1024 && 0x30 + (size_t)polysets * 48 <= size
		&& poly_clump_start <= size)
	{
		model_t *model = calloc (1, sizeof (model_t));
		if (!model)
			return NULL;

		model->meshes = calloc (polysets, sizeof (mesh_t));
		if (!model->meshes)
		{
			free (model);
			return NULL;
		}
		model->num_meshes = polysets;

		size_t poly_desc_offset = 0x30 + (size_t)polysets * 48;

		for (size_t oi = 0; oi < polysets; oi++)
		{
			mesh_t *mesh = &model->meshes[oi];
			const size_t obj_off = 0x30 + oi * 48;

			int32_t name_rel = is_be ? (int32_t)rd_be32 (data + obj_off + 32)
									 : (int32_t)rd_le32 (data + obj_off + 32);
			if (name_start + (size_t)name_rel < size && data[name_start + name_rel])
			{
				size_t nlen = strnlen ((const char *)(data + name_start + name_rel),
					sizeof (mesh->name) - 1);
				memcpy (mesh->name, data + name_start + name_rel, nlen);
				mesh->name[nlen] = '\0';
			}
			else
				snprintf (mesh->name, sizeof (mesh->name), "nud_mesh_%zu", oi);

			const uint16_t poly_count = is_be ? rd_be16 (data + obj_off + 42)
											  : rd_le16 (data + obj_off + 42);

			size_t total_verts = 0;
			size_t total_indices = 0;

			// First pass: count total vertices and indices across sub-polygons
			size_t temp_pd_off = poly_desc_offset;
			for (size_t pi = 0; pi < poly_count && temp_pd_off + 48 <= size; pi++)
			{
				const uint8_t *pd = data + temp_pd_off;
				uint16_t vcount = is_be ? rd_be16 (pd + 12) : rd_le16 (pd + 12);
				uint16_t pcount = is_be ? rd_be16 (pd + 32) : rd_le16 (pd + 32);
				uint8_t pflag = pd[35];

				total_verts += vcount;
				if (pflag == 4)
					total_indices += pcount;
				else if (pcount >= 3)
					total_indices += (pcount - 2) * 3;
				temp_pd_off += 48;
			}

			if (total_verts > 0)
			{
				mesh->positions = calloc (total_verts, sizeof (vec3_t));
				mesh->normals = calloc (total_verts, sizeof (vec3_t));
				mesh->texcoords = calloc (total_verts, sizeof (vec2_t));
				mesh->num_positions = total_verts;
				mesh->num_normals = total_verts;
				mesh->num_texcoords = total_verts;
			}
			if (total_indices > 0)
			{
				mesh->vertices = calloc (total_indices, sizeof (vertex_t));
				mesh->num_vertices = total_indices;
			}

			size_t vert_dest_idx = 0;
			size_t vert_out_idx = 0;

			for (size_t pi = 0; pi < poly_count && poly_desc_offset + 48 <= size; pi++)
			{
				const uint8_t *pd = data + poly_desc_offset;
				poly_desc_offset += 48;

				int32_t poly_start_rel
					= is_be ? (int32_t)rd_be32 (pd + 0) : (int32_t)rd_le32 (pd + 0);
				int32_t vert_start_rel
					= is_be ? (int32_t)rd_be32 (pd + 4) : (int32_t)rd_le32 (pd + 4);
				int32_t vertadd_start_rel
					= is_be ? (int32_t)rd_be32 (pd + 8) : (int32_t)rd_le32 (pd + 8);
				uint16_t vcount = is_be ? rd_be16 (pd + 12) : rd_le16 (pd + 12);
				uint8_t vert_size = pd[14];
				uint8_t uv_size = pd[15];
				uint16_t pcount = is_be ? rd_be16 (pd + 32) : rd_le16 (pd + 32);
				uint8_t poly_size = pd[34];
				uint8_t poly_flag = pd[35];

				size_t p_poly_start = poly_clump_start + (size_t)(poly_start_rel > 0 ? poly_start_rel : 0);
				size_t p_vert_start = vert_clump_start + (size_t)(vert_start_rel > 0 ? vert_start_rel : 0);
				size_t p_vertadd_start
					= vertadd_clump_start + (size_t)(vertadd_start_rel > 0 ? vertadd_start_rel : 0);

				int bone_type = vert_size & 0xF0;
				int vertex_type = vert_size & 0x0F;
				int uv_count = uv_size >> 4;
				int uv_type = uv_size & 0x0F;

				size_t uv_stride
					= (uv_type == 2 ? 4 : (uv_type == 4 ? 8 : 0)) + (size_t)uv_count * 4;
				if (uv_stride == 0)
					uv_stride = 4;

				size_t add_stride = 16;
				if (vertex_type == 1)
					add_stride = 32;
				else if (vertex_type == 2)
					add_stride = 64;
				else if (vertex_type == 3)
					add_stride = 48;

				const size_t sub_vert_base = vert_dest_idx;

				for (size_t vi = 0; vi < vcount && vert_dest_idx < total_verts; vi++)
				{
					if (bone_type > 0)
					{
						size_t ap = p_vertadd_start + vi * add_stride;
						if (ap + 12 <= size)
						{
							mesh->positions[vert_dest_idx].x = rd_f32 (data + ap, is_be);
							mesh->positions[vert_dest_idx].y = rd_f32 (data + ap + 4, is_be);
							mesh->positions[vert_dest_idx].z = rd_f32 (data + ap + 8, is_be);
						}
						if ((vertex_type == 1 || vertex_type == 2) && ap + 24 <= size)
						{
							mesh->normals[vert_dest_idx].x = rd_f32 (data + ap + 12, is_be);
							mesh->normals[vert_dest_idx].y = rd_f32 (data + ap + 16, is_be);
							mesh->normals[vert_dest_idx].z = rd_f32 (data + ap + 20, is_be);
						}
						else if (vertex_type == 3 && ap + 28 <= size)
						{
							mesh->normals[vert_dest_idx].x = rd_f32 (data + ap + 16, is_be);
							mesh->normals[vert_dest_idx].y = rd_f32 (data + ap + 20, is_be);
							mesh->normals[vert_dest_idx].z = rd_f32 (data + ap + 24, is_be);
						}
						else if (ap + 15 <= size)
						{
							mesh->normals[vert_dest_idx].x = (float)(int8_t)data[ap + 12] / 127.0f;
							mesh->normals[vert_dest_idx].y = (float)(int8_t)data[ap + 13] / 127.0f;
							mesh->normals[vert_dest_idx].z = (float)(int8_t)data[ap + 14] / 127.0f;
						}

						size_t uvp = p_vert_start + vi * uv_stride;
						size_t uv_data_off = uv_type == 2 ? 4 : (uv_type == 4 ? 8 : 0);
						if (uvp + uv_data_off + 4 <= size)
						{
							uint16_t u_raw = is_be ? rd_be16 (data + uvp + uv_data_off)
												   : rd_le16 (data + uvp + uv_data_off);
							uint16_t v_raw = is_be ? rd_be16 (data + uvp + uv_data_off + 2)
												   : rd_le16 (data + uvp + uv_data_off + 2);
							mesh->texcoords[vert_dest_idx].u = rd_f16 (u_raw);
							mesh->texcoords[vert_dest_idx].v = rd_f16 (v_raw);
						}
					}
					else
					{
						size_t vp = p_vert_start + vi * (add_stride + uv_stride);
						if (vp + 12 <= size)
						{
							mesh->positions[vert_dest_idx].x = rd_f32 (data + vp, is_be);
							mesh->positions[vert_dest_idx].y = rd_f32 (data + vp + 4, is_be);
							mesh->positions[vert_dest_idx].z = rd_f32 (data + vp + 8, is_be);
						}
						if (vp + 24 <= size)
						{
							mesh->normals[vert_dest_idx].x = rd_f32 (data + vp + 12, is_be);
							mesh->normals[vert_dest_idx].y = rd_f32 (data + vp + 16, is_be);
							mesh->normals[vert_dest_idx].z = rd_f32 (data + vp + 20, is_be);
						}
						if (vp + add_stride + 4 <= size)
						{
							uint16_t u_raw = is_be ? rd_be16 (data + vp + add_stride)
												   : rd_le16 (data + vp + add_stride);
							uint16_t v_raw = is_be ? rd_be16 (data + vp + add_stride + 2)
												   : rd_le16 (data + vp + add_stride + 2);
							mesh->texcoords[vert_dest_idx].u = rd_f16 (u_raw);
							mesh->texcoords[vert_dest_idx].v = rd_f16 (v_raw);
						}
					}
					vert_dest_idx++;
				}

				// Read indices
				if (poly_flag == 4)
				{
					// Triangles
					for (size_t ii = 0; ii < pcount && vert_out_idx < total_indices; ii++)
					{
						uint32_t idx = 0;
						if (poly_size == 4)
						{
							if (p_poly_start + ii < size)
								idx = data[p_poly_start + ii];
						}
						else
						{
							if (p_poly_start + ii * 2 + 2 <= size)
								idx = is_be ? rd_be16 (data + p_poly_start + ii * 2)
											: rd_le16 (data + p_poly_start + ii * 2);
						}
						size_t final_idx = sub_vert_base + idx;
						if (final_idx >= total_verts)
							final_idx = sub_vert_base;

						mesh->vertices[vert_out_idx].position_idx = (int)final_idx;
						mesh->vertices[vert_out_idx].normal_idx = (int)final_idx;
						mesh->vertices[vert_out_idx].texcoord_idx = (int)final_idx;
						vert_out_idx++;
					}
				}
				else if (pcount >= 3)
				{
					// Triangle strip
					uint32_t *raw_idx = calloc (pcount, sizeof (uint32_t));
					if (raw_idx)
					{
						for (size_t ii = 0; ii < pcount; ii++)
						{
							if (poly_size == 4)
								raw_idx[ii] = p_poly_start + ii < size ? data[p_poly_start + ii] : 0;
							else
								raw_idx[ii] = p_poly_start + ii * 2 + 2 <= size
									? (is_be ? rd_be16 (data + p_poly_start + ii * 2)
											 : rd_le16 (data + p_poly_start + ii * 2))
									: 0;
						}
						for (size_t ii = 2; ii < pcount && vert_out_idx + 3 <= total_indices; ii++)
						{
							uint32_t i0 = raw_idx[ii - 2];
							uint32_t i1 = raw_idx[ii - 1];
							uint32_t i2 = raw_idx[ii];
							if (i0 == 0xffff || i1 == 0xffff || i2 == 0xffff)
								continue;
							if (i0 == i1 || i1 == i2 || i0 == i2)
								continue;

							uint32_t tri[3];
							if (ii & 1)
							{
								tri[0] = i1;
								tri[1] = i0;
								tri[2] = i2;
							}
							else
							{
								tri[0] = i0;
								tri[1] = i1;
								tri[2] = i2;
							}
							for (int t = 0; t < 3; t++)
							{
								size_t f_idx = sub_vert_base + tri[t];
								if (f_idx >= total_verts)
									f_idx = sub_vert_base;
								mesh->vertices[vert_out_idx].position_idx = (int)f_idx;
								mesh->vertices[vert_out_idx].normal_idx = (int)f_idx;
								mesh->vertices[vert_out_idx].texcoord_idx = (int)f_idx;
								vert_out_idx++;
							}
						}
						free (raw_idx);
					}
				}
			}
			mesh->num_vertices = vert_out_idx;
		}
		return model;
	}

	// Fallback to flat/simple layout for legacy or synthetic files
	const uint32_t poly_sz = is_be ? rd_be32 (data + 0x10) : rd_le32 (data + 0x10);
	const uint32_t tri_sz = is_be ? rd_be32 (data + 0x14) : rd_le32 (data + 0x14);
	const uint32_t vert_sz = is_be ? rd_be32 (data + 0x18) : rd_le32 (data + 0x18);

	const size_t poly_start = 0x30;
	const size_t tri_start = poly_start + poly_sz;
	const size_t vert_start = tri_start + tri_sz;

	if (tri_start + tri_sz > size || vert_start > size)
		return NULL;

	const size_t num_indices = tri_sz / 2;
	if (!num_indices)
		return NULL;

	uint16_t max_idx = 0;
	for (size_t i = 0; i < num_indices; i++)
	{
		uint16_t idx
			= is_be ? rd_be16 (data + tri_start + i * 2) : rd_le16 (data + tri_start + i * 2);
		if (idx > max_idx)
			max_idx = idx;
	}

	size_t stride = 24;
	size_t num_verts_expected = max_idx + 1;
	if (num_verts_expected > 0)
	{
		if (vert_sz / 32 >= num_verts_expected && vert_sz % 32 == 0)
			stride = 32;
		else if (vert_sz / 48 >= num_verts_expected && vert_sz % 48 == 0)
			stride = 48;
		else if (vert_sz / 24 >= num_verts_expected)
			stride = 24;
	}

	size_t vert_count = (size - vert_start) / stride;
	if (vert_sz / stride < vert_count)
		vert_count = vert_sz / stride;
	if (!vert_count)
		return NULL;

	model_t *model = calloc (1, sizeof (model_t));
	if (!model)
		return NULL;

	model->meshes = calloc (1, sizeof (mesh_t));
	if (!model->meshes)
	{
		free (model);
		return NULL;
	}

	model->num_meshes = 1;
	mesh_t *mesh = &model->meshes[0];
	snprintf (mesh->name, sizeof (mesh->name), "nud_mesh");

	mesh->positions = calloc (vert_count, sizeof (vec3_t));
	mesh->normals = calloc (vert_count, sizeof (vec3_t));
	mesh->texcoords = calloc (vert_count, sizeof (vec2_t));
	mesh->vertices = calloc (num_indices, sizeof (vertex_t));
	mesh->num_positions = vert_count;
	mesh->num_normals = vert_count;
	mesh->num_texcoords = vert_count;
	mesh->num_vertices = num_indices;

	for (size_t vi = 0; vi < vert_count; vi++)
	{
		size_t vp = vert_start + vi * stride;
		if (vp + 12 <= size)
		{
			mesh->positions[vi].x = rd_f32 (data + vp, is_be);
			mesh->positions[vi].y = rd_f32 (data + vp + 4, is_be);
			mesh->positions[vi].z = rd_f32 (data + vp + 8, is_be);
		}

		if (stride >= 24 && vp + 20 <= size)
		{
			mesh->normals[vi].x = (float)(int8_t)data[vp + 16] / 127.0f;
			mesh->normals[vi].y = (float)(int8_t)data[vp + 17] / 127.0f;
			mesh->normals[vi].z = (float)(int8_t)data[vp + 18] / 127.0f;
		}

		if (stride >= 24 && vp + stride <= size)
		{
			uint16_t u_raw
				= is_be ? rd_be16 (data + vp + (stride - 4)) : rd_le16 (data + vp + (stride - 4));
			uint16_t v_raw
				= is_be ? rd_be16 (data + vp + (stride - 2)) : rd_le16 (data + vp + (stride - 2));
			mesh->texcoords[vi].u = rd_f16 (u_raw);
			mesh->texcoords[vi].v = rd_f16 (v_raw);
		}
	}

	for (size_t ii = 0; ii < num_indices; ii++)
	{
		uint16_t idx
			= is_be ? rd_be16 (data + tri_start + ii * 2) : rd_le16 (data + tri_start + ii * 2);
		if (idx < vert_count)
		{
			mesh->vertices[ii].position_idx = idx;
			mesh->vertices[ii].normal_idx = idx;
			mesh->vertices[ii].texcoord_idx = idx;
		}
	}

	return model;
}

int EncodeModelToNUD (const model_t *model, const char *out_nud_path)
{
	if (!model || !model->num_meshes || !out_nud_path)
		return ERR_INVALID_DATA;

	const mesh_t *mesh = &model->meshes[0];
	if (!mesh->num_positions || !mesh->num_vertices)
		return ERR_INVALID_DATA;

	const size_t poly_sz = 0x360; // Standard poly block header size
	const size_t num_indices = mesh->num_vertices;
	const size_t tri_sz = (num_indices * 2 + 0x1F) & ~0x1F;
	const size_t stride = 24;
	const size_t vert_count = mesh->num_positions;
	const size_t vert_sz = (vert_count * stride + 0x1F) & ~0x1F;
	const size_t total_sz = 0x30 + poly_sz + tri_sz + vert_sz;

	uint8_t *buf = calloc (1, total_sz);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	// Header (0x30 bytes)
	memcpy (buf, "NDP3", 4);
	wr_be16 (buf + 4, 0); // version
	wr_be16 (buf + 6, 20); // num_polygroups
	wr_be16 (buf + 8, 2); // poly_type
	wr_be16 (buf + 10, 4); // num_bones

	wr_be32 (buf + 0x0C, 0x00010004);
	wr_be32 (buf + 0x10, (uint32_t)poly_sz);
	wr_be32 (buf + 0x14, (uint32_t)tri_sz);
	wr_be32 (buf + 0x18, (uint32_t)vert_sz);
	wr_be32 (buf + 0x1C, 0); // vert_add_sz

	// Bounding sphere
	float min_p[3] = { 1e9f, 1e9f, 1e9f };
	float max_p[3] = { -1e9f, -1e9f, -1e9f };
	for (size_t i = 0; i < vert_count; i++)
	{
		if (mesh->positions[i].x < min_p[0])
			min_p[0] = mesh->positions[i].x;
		if (mesh->positions[i].y < min_p[1])
			min_p[1] = mesh->positions[i].y;
		if (mesh->positions[i].z < min_p[2])
			min_p[2] = mesh->positions[i].z;
		if (mesh->positions[i].x > max_p[0])
			max_p[0] = mesh->positions[i].x;
		if (mesh->positions[i].y > max_p[1])
			max_p[1] = mesh->positions[i].y;
		if (mesh->positions[i].z > max_p[2])
			max_p[2] = mesh->positions[i].z;
	}
	float cx = (min_p[0] + max_p[0]) * 0.5f;
	float cy = (min_p[1] + max_p[1]) * 0.5f;
	float cz = (min_p[2] + max_p[2]) * 0.5f;
	float rad = sqrtf ((max_p[0] - cx) * (max_p[0] - cx) + (max_p[1] - cy) * (max_p[1] - cy)
		+ (max_p[2] - cz) * (max_p[2] - cz));

	wr_f32 (buf + 0x20, cx, true);
	wr_f32 (buf + 0x24, cy, true);
	wr_f32 (buf + 0x28, cz, true);
	wr_f32 (buf + 0x2C, rad, true);

	// Triangles / Indices
	const size_t tri_start = 0x30 + poly_sz;
	for (size_t i = 0; i < num_indices; i++)
	{
		uint16_t idx = (uint16_t)mesh->vertices[i].position_idx;
		wr_be16 (buf + tri_start + i * 2, idx);
	}

	// Vertices
	const size_t vert_start = tri_start + tri_sz;
	for (size_t i = 0; i < vert_count; i++)
	{
		size_t vp = vert_start + i * stride;
		wr_f32 (buf + vp, mesh->positions[i].x, true);
		wr_f32 (buf + vp + 4, mesh->positions[i].y, true);
		wr_f32 (buf + vp + 8, mesh->positions[i].z, true);
		wr_f32 (buf + vp + 12, 1.0f, true);

		if (mesh->normals)
		{
			buf[vp + 16] = (uint8_t)(int8_t)(mesh->normals[i].x * 127.0f);
			buf[vp + 17] = (uint8_t)(int8_t)(mesh->normals[i].y * 127.0f);
			buf[vp + 18] = (uint8_t)(int8_t)(mesh->normals[i].z * 127.0f);
			buf[vp + 19] = 0x7F;
		}

		if (mesh->texcoords)
		{
			wr_be16 (buf + vp + 20, wr_f16 (mesh->texcoords[i].u));
			wr_be16 (buf + vp + 22, wr_f16 (mesh->texcoords[i].v));
		}
	}

	FILE *f = fopen (out_nud_path, "wb");
	if (!f)
	{
		free (buf);
		return ERR_CANT_CREATE;
	}

	fwrite (buf, 1, total_sz, f);
	fclose (f);
	free (buf);

	return ERR_OK;
}
