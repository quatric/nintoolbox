#include "lib-std.h"
#include "lib-nud.h"
#include <string.h>
#include <stdio.h>

static uint16_t rd_be16 (const uint8_t *p) { return (uint16_t)p[0] << 8 | p[1]; }
static uint32_t rd_be32 (const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t rd_le16 (const uint8_t *p) { return (uint16_t)p[1] << 8 | p[0]; }
static uint32_t rd_le32 (const uint8_t *p) { return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }

static float rd_f32 (const uint8_t *p, bool is_be)
{
	union { uint32_t u; float f; } c;
	c.u = is_be ? rd_be32 (p) : rd_le32 (p);
	return c.f;
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
		while (e > 0) { v *= 2.0f; e--; }
		while (e < 0) { v /= 2.0f; e++; }
	}
	return sign ? -v : v;
}

bool IsNUD (const uint8_t *data, size_t size)
{
	if (!data || size < 16)
		return false;
	return !memcmp (data, "NDP3", 4) || !memcmp (data, "NDWU", 4);
}

model_t *ParseNUD (const uint8_t *data, size_t size)
{
	if (!IsNUD (data, size))
		return NULL;

	const bool is_be = !memcmp (data, "NDWU", 4);
	const uint16_t num_polysets = is_be ? rd_be16 (data + 6) : rd_le16 (data + 6);
	const uint16_t num_meshes = is_be ? rd_be16 (data + 10) : rd_le16 (data + 10);
	const uint32_t num_tris = is_be ? rd_be32 (data + 12) : rd_le32 (data + 12);
	(void)num_polysets; (void)num_tris;

	if (!num_meshes || num_meshes > 0x1000)
		return NULL;

	model_t *model = CALLOC (1, sizeof (model_t));
	if (!model)
		return NULL;

	model->meshes = CALLOC (num_meshes, sizeof (mesh_t));
	if (!model->meshes)
	{
		FREE (model);
		return NULL;
	}

	size_t header_pos = 16;
	for (uint16_t m = 0; m < num_meshes; m++)
	{
		if (header_pos + 0x30 > size)
			break;

		mesh_t *mesh = &model->meshes[model->num_meshes++];
		char name[64];
		snprintf (name, sizeof (name), "Mesh_%u", m);
		snprintf (mesh->name, sizeof (mesh->name), "%s", name);

		const uint16_t poly_count = is_be ? rd_be16 (data + header_pos + 12) : rd_le16 (data + header_pos + 12);
		header_pos += 0x30;

		for (uint16_t p = 0; p < poly_count; p++)
		{
			if (header_pos + 0x30 > size)
				break;

			const uint32_t poly_start = is_be ? rd_be32 (data + header_pos) : rd_le32 (data + header_pos);
			const uint32_t vert_start = is_be ? rd_be32 (data + header_pos + 4) : rd_le32 (data + header_pos + 4);
			const uint32_t vert_add = is_be ? rd_be32 (data + header_pos + 8) : rd_le32 (data + header_pos + 8);
			const uint16_t vert_count = is_be ? rd_be16 (data + header_pos + 12) : rd_le16 (data + header_pos + 12);
			const uint8_t vert_type = data[header_pos + 14];
			const uint8_t vert_size = data[header_pos + 15];
			header_pos += 0x30;

			if (poly_start >= size || vert_start >= size || !vert_count)
				continue;

			// Allocate mesh buffers if needed
			if (!mesh->positions)
			{
				mesh->positions = CALLOC (vert_count, sizeof (vec3_t));
				mesh->normals = CALLOC (vert_count, sizeof (vec3_t));
				mesh->texcoords = CALLOC (vert_count, sizeof (vec2_t));
				mesh->vertices = CALLOC (vert_count, sizeof (vertex_t));
				mesh->num_positions = vert_count;
				mesh->num_normals = vert_count;
				mesh->num_texcoords = vert_count;
				mesh->num_vertices = vert_count;

				for (uint16_t vi = 0; vi < vert_count; vi++)
				{
					size_t vp = vert_start + vi * (vert_size ? vert_size : 48);
					if (vp + 12 <= size)
					{
						mesh->positions[vi].x = rd_f32 (data + vp, is_be);
						mesh->positions[vi].y = rd_f32 (data + vp + 4, is_be);
						mesh->positions[vi].z = rd_f32 (data + vp + 8, is_be);
					}
					if (vp + 24 <= size)
					{
						mesh->normals[vi].x = rd_f32 (data + vp + 12, is_be);
						mesh->normals[vi].y = rd_f32 (data + vp + 16, is_be);
						mesh->normals[vi].z = rd_f32 (data + vp + 20, is_be);
					}
					if (vp + 32 <= size)
					{
						mesh->texcoords[vi].u = rd_f32 (data + vp + 24, is_be);
						mesh->texcoords[vi].v = rd_f32 (data + vp + 28, is_be);
					}
					mesh->vertices[vi].position_idx = vi;
					mesh->vertices[vi].normal_idx = vi;
					mesh->vertices[vi].texcoord_idx = vi;
				}
			}
			(void)vert_add; (void)vert_type;
		}
	}

	return model;
}
