#include "lib-std.h"
#include "lib-numsh.h"
#include <string.h>
#include <stdio.h>

static uint16_t rd_le16 (const uint8_t *p) { return (uint16_t)p[1] << 8 | p[0]; }
static uint32_t rd_le32 (const uint8_t *p) { return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }
static uint64_t rd_le64 (const uint8_t *p) { return (uint64_t)rd_le32 (p + 4) << 32 | rd_le32 (p); }

static float rd_f32 (const uint8_t *p)
{
	union { uint32_t u; float f; } c;
	c.u = rd_le32 (p);
	return c.f;
}

bool IsSSBH (const uint8_t *data, size_t size)
{
	if (!data || size < 16)
		return false;
	return !memcmp (data, "HBSS", 4) || !memcmp (data, "SSBH", 4);
}

model_t *ParseNUMSHB (const uint8_t *data, size_t size)
{
	if (!IsSSBH (data, size))
		return NULL;

	// SSBH header: [0..4] magic, [4..6] major, [6..8] minor, [8..16] root_offset
	const uint64_t root_off = rd_le64 (data + 8);
	if (root_off >= size || root_off + 32 > size)
		return NULL;

	// Mesh Root: [0..8] model_name_off, [8..16] objects_off, [16..24] objects_count
	const uint64_t objects_off = rd_le64 (data + root_off + 8);
	const uint64_t objects_count = rd_le64 (data + root_off + 16);

	if (!objects_count || objects_count > 0x1000 || objects_off >= size)
		return NULL;

	model_t *model = CALLOC (1, sizeof (model_t));
	if (!model)
		return NULL;

	model->meshes = CALLOC (objects_count, sizeof (mesh_t));
	if (!model->meshes)
	{
		FREE (model);
		return NULL;
	}

	for (uint64_t i = 0; i < objects_count; i++)
	{
		const uint64_t obj_off = objects_off + i * 168; // MeshObject layout
		if (obj_off + 168 > size)
			break;

		mesh_t *mesh = &model->meshes[model->num_meshes++];
		snprintf (mesh->name, sizeof (mesh->name), "Mesh_%llu", (unsigned long long)i);

		const uint64_t vert_count = rd_le64 (data + obj_off + 48);
		const uint64_t vert_stride = rd_le64 (data + obj_off + 56);
		const uint64_t vert_buf_off = rd_le64 (data + obj_off + 64);
		const uint64_t idx_count = rd_le64 (data + obj_off + 88);
		const uint64_t idx_buf_off = rd_le64 (data + obj_off + 96);

		if (vert_count && vert_buf_off < size && vert_buf_off + vert_count * (vert_stride ? vert_stride : 48) <= size)
		{
			mesh->positions = CALLOC (vert_count, sizeof (vec3_t));
			mesh->normals = CALLOC (vert_count, sizeof (vec3_t));
			mesh->texcoords = CALLOC (vert_count, sizeof (vec2_t));
			mesh->vertices = CALLOC (vert_count, sizeof (vertex_t));
			mesh->num_positions = vert_count;
			mesh->num_normals = vert_count;
			mesh->num_texcoords = vert_count;
			mesh->num_vertices = vert_count;

			const uint64_t stride = vert_stride ? vert_stride : 48;
			for (uint64_t vi = 0; vi < vert_count; vi++)
			{
				const uint64_t vp = vert_buf_off + vi * stride;
				if (vp + 12 <= size)
				{
					mesh->positions[vi].x = rd_f32 (data + vp);
					mesh->positions[vi].y = rd_f32 (data + vp + 4);
					mesh->positions[vi].z = rd_f32 (data + vp + 8);
				}
				if (vp + 24 <= size)
				{
					mesh->normals[vi].x = rd_f32 (data + vp + 12);
					mesh->normals[vi].y = rd_f32 (data + vp + 16);
					mesh->normals[vi].z = rd_f32 (data + vp + 20);
				}
				if (vp + 32 <= size)
				{
					mesh->texcoords[vi].u = rd_f32 (data + vp + 24);
					mesh->texcoords[vi].v = rd_f32 (data + vp + 28);
				}
				mesh->vertices[vi].position_idx = (int)vi;
				mesh->vertices[vi].normal_idx = (int)vi;
				mesh->vertices[vi].texcoord_idx = (int)vi;
			}
		}

		(void)idx_count; (void)idx_buf_off;
	}

	return model;
}
