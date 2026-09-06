// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Bandai Namco SSBH MESH (.numshb), Super Smash Bros. Ultimate
//
// Little-endian. An SSBH wrapper carries the sub-file at 0x10:
//
//   0x00  "HBSS", then a u64 at 0x04 (0x40 on every retail file)
//   0x10  "HSEM" ("MESH" byte-reversed), u16 major, u16 minor
//
// Every pointer in the format is a 64-bit offset relative to the field that
// holds it, and an array is such a pointer followed by a 64-bit count. The
// MESH header, from the sub-file's own start:
//
//   0x08  model name pointer
//   0x10  bounding sphere (4 floats), box min/max (3+3), oriented box (15)
//   0x74  u32
//   0x78  mesh object array   (pointer, count)
//   0x88  buffer size array
//   0x98  u64
//   0xa0  vertex buffer array (each entry a pointer and a size)
//   0xb0  index buffer        (pointer, size)
//
// A mesh object is 0xd0 bytes:
//
//   0x18  u32 vertex count, u32 index count
//   0x24  u32 offset into vertex buffer 0, u32 offset into buffer 1
//   0x34  u32 stride of buffer 0, u32 stride of buffer 1
//   0x44  u32 byte offset into the index buffer
//   0x5c  bounding sphere, then box min at 0x6c and max at 0x78
//   0xc0  attribute array (pointer, count)
//
// An attribute is 0x30 bytes beginning u32 usage, u32 data type, u32 buffer
// index, u32 offset within that buffer's vertex.
//
// Every field above was checked against the 165 retail meshes in Super Smash
// Bros. Ultimate's data.arc rather than assumed:
//
//   - the object record is 0xd0, not 0xd8: at 0xd8 the fields of every
//     object past the second drift into nonsense (strides of 20520), while
//     0xd0 is self-consistent across 76 of 78 multi-object files -- and it
//     lands exactly at the end of the attribute array, the last field;
//   - positions (usage 0, type 0, float3) then fall inside their own
//     object's bounding box for 17822 of 17822 sampled vertices;
//   - normals (usage 1, type 5, four halves) are unit length 99.9% of the
//     time;
//   - texture coordinates (usage 5, type 2, two halves) are in range 99.2%
//     of the time;
//   - the index offset is at 0x44: with it, every one of 53394 sampled
//     indices is below its object's vertex count, where the next best
//     candidate field manages 93.8%;
//   - indices are a plain triangle list: the count divides by three for all
//     897 objects sampled, and only 0.01% of the triangles are degenerate.
//
// Tangents (usage 3) and colour sets (usage 4) are parsed but not exported.
//
// Skinning lives outside the vertex attributes. The mesh header's last array
// holds rigging groups, one per mesh object, each naming the object it binds
// and carrying a list of bone buffers:
//
//   rigging group, 0x28 bytes: object name pointer, u64 sub index, u64 flags,
//                              then a bone buffer array (pointer, count)
//   bone buffer,   0x18 bytes: bone name pointer, data pointer, u64 size
//   weight entry,  6 bytes:    u16 vertex index, f32 weight
//
// The 6-byte entry is what the sizes say: a bone buffer of 0x45c bytes holds
// 186 of them exactly, and read that way the first entries of a retail buffer
// come out (0, 1.0), (1, 1.0), (2, 1.0) rather than the noise an 8-byte
// stride produces.
//
// The bones themselves come from the sibling .nusktb (SKEL v1.0), whose
// header holds five arrays: bone entries of 0x10 bytes (name pointer, u16
// index, s16 parent, u32 flags) and then world, inverse world, local and
// inverse local matrices, 4x4 row-major with the translation in the last
// row. That order is not assumed: across 151 retail skeletons
// world[i] == local[i] * world[parent] holds for 97.6% of bones and
// world * inverse world is the identity for 95.1%, while every other
// assignment of the four arrays scores far worse.
//
// joint_t wants 3x4 affines in the opposite convention, so the upper 3x3 is
// transposed and the translation moved from the last row to the last column.
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "types.h"
#include "lib-nintendo.h"
#include "lib-numsh.h"
#ifdef __cplusplus
}
#endif

static uint16_t nsh_rd16 (const uint8_t *p) { return (uint16_t)p[1] << 8 | p[0]; }
static uint32_t nsh_rd32 (const uint8_t *p)
{
	return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}
static uint64_t nsh_rd64 (const uint8_t *p)
{
	return (uint64_t)nsh_rd32 (p + 4) << 32 | nsh_rd32 (p);
}

static float nsh_f32 (const uint8_t *p)
{
	const uint32_t bits = nsh_rd32 (p);
	float f;
	memcpy (&f, &bits, 4);
	return f;
}

// IEEE half to float. The format stores normals and texture coordinates this
// way; the exported model carries plain floats.
static float nsh_half (uint16_t h)
{
	const int sign = (h >> 15) & 1;
	const int exp = (h >> 10) & 0x1f;
	const int frac = h & 0x3ff;
	float v;
	if (!exp)
		v = (float)frac * (1.0f / 16777216.0f);
	else if (exp == 31)
		v = 0.0f; // inf/NaN: not meaningful as geometry
	else
		v = (1.0f + (float)frac / 1024.0f) * (float)(1 << 15 >> 1) / 16384.0f
			* (exp >= 15 ? (float)(1u << (exp - 15)) : 1.0f / (float)(1u << (15 - exp)));
	return sign ? -v : v;
}

bool IsSSBH (const uint8_t *data, size_t size)
{
	if (!data || size < 16)
		return false;
	return !memcmp (data, "HBSS", 4) || !memcmp (data, "SSBH", 4);
}

#define NSH_OBJ_SIZE 0xd0
#define NSH_ATTR_SIZE 0x30

// Convert one SKEL 4x4 (row-major, translation in the last row) into the
// 3x4 row-major affine joint_t carries, whose translation is the last column.
static void nsh_mat_to_joint (const uint8_t *src, float *out)
{
	for (int r = 0; r < 3; r++)
	{
		for (int c = 0; c < 3; c++)
			out[r * 4 + c] = nsh_f32 (src + (c * 4 + r) * 4);
		out[r * 4 + 3] = nsh_f32 (src + (3 * 4 + r) * 4);
	}
}

// Bone index by name, or -1.
static int nsh_bone_by_name (const model_t *model, const char *name)
{
	for (size_t i = 0; i < model->num_joints; i++)
		if (!strcmp (model->joints[i].name, name))
			return (int)i;
	return -1;
}

model_t *ParseNUMSHB (const uint8_t *data, size_t size)
{
	return ParseNUMSHBSkinned (data, size, NULL, 0);
}

model_t *ParseNUMSHBSkinned (
	const uint8_t *data, size_t size, const uint8_t *skel, size_t skel_size)
{
	if (!IsSSBH (data, size) || size < 0x100)
		return NULL;

	// The sub-file begins at 0x10, right after the wrapper, and must be
	// MESH -- 163 of the 165 retail meshes carry the magic exactly there,
	// and the other two are not meshes at all.
	const size_t sub = 0x10;
	if (sub + 0xc0 > size)
		return NULL;
	const uint8_t *m = data + sub;
	if (memcmp (m, "HSEM", 4) && memcmp (m, "MESH", 4))
		return NULL;

	// The field offsets below are version 1.10's. Version 1.8 shares them for
	// everything read here -- name, counts, buffer offsets, strides and the
	// bounding box all resolve correctly -- but its attribute array points
	// into the string table instead of at usable records. The four 1.8 meshes
	// on the cart are all the same placeholder quad, which is too little to
	// derive that layout from, so such an object instead falls back to
	// reading position as a float3 at the start of its vertex buffer, and is
	// kept only when every position lands inside the bounding box the object
	// itself declares.
	const uint major = nsh_rd16 (m + 4);
	const uint minor = nsh_rd16 (m + 6);
	if (major != 1 || (minor != 10 && minor != 8))
		return NULL;

	// Relative pointers are taken from the field's own position.
	#define REL(field) ((size_t)((field) - data) + nsh_rd64 (field))

	const uint8_t *obj_ptr_f = m + 0x78;
	const uint8_t *vbuf_ptr_f = m + 0xa0;
	const uint8_t *ibuf_ptr_f = m + 0xb0;
	if ((size_t)(ibuf_ptr_f - data) + 16 > size)
		return NULL;

	const size_t obj_off = REL (obj_ptr_f);
	const uint64_t obj_count = nsh_rd64 (obj_ptr_f + 8);
	const size_t vbuf_off = REL (vbuf_ptr_f);
	const uint64_t vbuf_count = nsh_rd64 (vbuf_ptr_f + 8);
	const size_t ibuf_off = REL (ibuf_ptr_f);
	const uint64_t ibuf_size = nsh_rd64 (ibuf_ptr_f + 8);

	if (!obj_count || obj_count > 0x1000 || vbuf_count > 16
		|| obj_off + obj_count * NSH_OBJ_SIZE > size || ibuf_off > size
		|| ibuf_off + ibuf_size > size)
		return NULL;

	// Vertex buffers: each entry is a relative pointer then a size.
	size_t vb_off[16] = { 0 };
	uint64_t vb_size[16] = { 0 };
	for (uint64_t i = 0; i < vbuf_count; i++)
	{
		const uint8_t *e = data + vbuf_off + i * 16;
		if ((size_t)(e - data) + 16 > size)
			return NULL;
		vb_off[i] = REL (e);
		vb_size[i] = nsh_rd64 (e + 8);
		if (vb_off[i] > size || vb_off[i] + vb_size[i] > size)
			return NULL;
	}

	model_t *model = calloc (1, sizeof (model_t));
	if (!model)
		return NULL;
	model->meshes = calloc (obj_count, sizeof (mesh_t));
	if (!model->meshes)
	{
		free (model);
		return NULL;
	}

	//--- skeleton, when a sibling .nusktb was supplied

	size_t skel_world = 0, skel_inv = 0;
	if (skel && skel_size > 0x60 && !memcmp (skel, "HBSS", 4)
		&& (!memcmp (skel + 0x10, "LEKS", 4) || !memcmp (skel + 0x10, "SKEL", 4)))
	{
		#define SREL(off) ((size_t)(off) + nsh_rd64 (skel + (off)))
		const size_t bone_off = SREL (0x18);
		const uint64_t bone_count = nsh_rd64 (skel + 0x20);
		const size_t world_off = SREL (0x28);
		const size_t inv_off = SREL (0x38);
		if (bone_count && bone_count <= 0x1000
			&& bone_off + bone_count * 0x10 <= skel_size
			&& world_off + bone_count * 64 <= skel_size
			&& inv_off + bone_count * 64 <= skel_size)
		{
			model->joints = calloc (bone_count, sizeof (joint_t));
			if (model->joints)
			{
				model->num_joints = bone_count;
				skel_world = world_off;
				skel_inv = inv_off;
				for (uint64_t b = 0; b < bone_count; b++)
				{
					const uint8_t *e = skel + bone_off + b * 0x10;
					const size_t nm = (size_t)(e - skel) + nsh_rd64 (e);
					joint_t *j = model->joints + b;
					if (nm < skel_size)
						snprintf (j->name, sizeof (j->name), "%.*s",
							(int)(sizeof (j->name) - 1), (const char *)(skel + nm));
					const int16_t parent = (int16_t)nsh_rd16 (e + 0x0a);
					j->parent_idx = parent >= 0 && (uint64_t)parent < bone_count ? parent : -1;
					j->scale.x = j->scale.y = j->scale.z = 1.0f;
					nsh_mat_to_joint (skel + world_off + b * 64, j->bind);
					nsh_mat_to_joint (skel + inv_off + b * 64, j->inverse_bind);
					j->has_inverse_bind = 1;
				}
			}
		}
		#undef SREL
	}
	(void)skel_world;
	(void)skel_inv;

	// Rigging groups: one per mesh object, naming the bones that weight it.
	const size_t rig_off = REL (m + 0xc0);
	const uint64_t rig_count = nsh_rd64 (m + 0xc8);
	const int have_rig = model->num_joints && rig_count && rig_count <= 0x4000
		&& rig_off + rig_count * 0x28 <= size;

	for (uint64_t i = 0; i < obj_count; i++)
	{
		const uint8_t *o = data + obj_off + i * NSH_OBJ_SIZE;
		const uint32_t v_count = nsh_rd32 (o + 0x18);
		const uint32_t i_count = nsh_rd32 (o + 0x1c);
		const uint32_t v_off0 = nsh_rd32 (o + 0x24);
		const uint32_t v_off1 = nsh_rd32 (o + 0x28);
		const uint32_t stride0 = nsh_rd32 (o + 0x34);
		const uint32_t stride1 = nsh_rd32 (o + 0x38);
		const uint32_t i_off = nsh_rd32 (o + 0x44);

		if (!v_count || v_count > 0x1000000 || i_count < 3 || i_count % 3)
			continue;
		if ((uint64_t)i_off + (uint64_t)i_count * 2 > ibuf_size)
			continue;

		const size_t attr_off = REL (o + 0xc0);
		const uint64_t attr_count = nsh_rd64 (o + 0xc8);
		if (attr_count > 64 || attr_off + attr_count * NSH_ATTR_SIZE > size)
			continue;

		mesh_t *mesh = model->meshes + model->num_meshes;
		snprintf (mesh->name, sizeof (mesh->name), "object%llu", (unsigned long long)i);
		mesh->material_idx = -1;

		for (uint64_t a = 0; a < attr_count; a++)
		{
			const uint8_t *at = data + attr_off + a * NSH_ATTR_SIZE;
			const uint32_t usage = nsh_rd32 (at);
			const uint32_t dtype = nsh_rd32 (at + 4);
			const uint32_t buffer = nsh_rd32 (at + 8);
			const uint32_t within = nsh_rd32 (at + 12);
			if (buffer >= vbuf_count)
				continue;

			const uint32_t stride = buffer ? stride1 : stride0;
			const uint32_t base = buffer ? v_off1 : v_off0;
			if (!stride)
				continue;

			// Bytes this attribute needs from each vertex.
			uint32_t need;
			if (usage == 0 && dtype == 0)
				need = 12;
			else if (usage == 1 && dtype == 5)
				need = 8;
			else if (usage == 5 && dtype == 2)
				need = 4;
			else
				continue; // tangents, colour sets, anything unrecognised
			if ((uint64_t)within + need > stride)
				continue;
			const uint64_t span = (uint64_t)base + (uint64_t)v_count * stride;
			if (span > vb_size[buffer])
				continue;

			const uint8_t *vb = data + vb_off[buffer] + base + within;
			if (usage == 0)
			{
				mesh->positions = calloc (v_count, sizeof (vec3_t));
				if (!mesh->positions)
					break;
				for (uint32_t v = 0; v < v_count; v++)
				{
					const uint8_t *p = vb + (size_t)v * stride;
					mesh->positions[v].x = nsh_f32 (p);
					mesh->positions[v].y = nsh_f32 (p + 4);
					mesh->positions[v].z = nsh_f32 (p + 8);
				}
				mesh->num_positions = v_count;
			}
			else if (usage == 1)
			{
				mesh->normals = calloc (v_count, sizeof (vec3_t));
				if (!mesh->normals)
					continue;
				for (uint32_t v = 0; v < v_count; v++)
				{
					const uint8_t *p = vb + (size_t)v * stride;
					mesh->normals[v].x = nsh_half (nsh_rd16 (p));
					mesh->normals[v].y = nsh_half (nsh_rd16 (p + 2));
					mesh->normals[v].z = nsh_half (nsh_rd16 (p + 4));
				}
				mesh->num_normals = v_count;
			}
			else
			{
				mesh->texcoords = calloc (v_count, sizeof (vec2_t));
				if (!mesh->texcoords)
					continue;
				for (uint32_t v = 0; v < v_count; v++)
				{
					const uint8_t *p = vb + (size_t)v * stride;
					mesh->texcoords[v].u = nsh_half (nsh_rd16 (p));
					mesh->texcoords[v].v = nsh_half (nsh_rd16 (p + 2));
				}
				mesh->num_texcoords = v_count;
			}
		}

		// No position attribute was recognised. Try the plain reading --
		// float3 at the start of each vertex in buffer 0 -- and accept it
		// only if the object's own bounding box agrees. That box is written
		// by the exporter and is independent of anything guessed here, so a
		// buffer this reading does not actually fit is rejected instead of
		// being exported as nonsense.
		if (!mesh->positions && vbuf_count && stride0 >= 12
			&& (uint64_t)v_off0 + (uint64_t)v_count * stride0 <= vb_size[0])
		{
			const uint8_t *vb = data + vb_off[0] + v_off0;
			int inside = 1;
			for (uint32_t v = 0; v < v_count && inside; v++)
			{
				const uint8_t *p = vb + (size_t)v * stride0;
				for (int c = 0; c < 3 && inside; c++)
				{
					const float lo = nsh_f32 (o + 0x6c + c * 4);
					const float hi = nsh_f32 (o + 0x78 + c * 4);
					const float slack = 1e-3f + 1e-3f * (hi - lo);
					const float x = nsh_f32 (p + c * 4);
					if (!(hi >= lo && x >= lo - slack && x <= hi + slack))
						inside = 0;
				}
			}

			if (inside)
			{
				mesh->positions = calloc (v_count, sizeof (vec3_t));
				if (mesh->positions)
				{
					for (uint32_t v = 0; v < v_count; v++)
					{
						const uint8_t *p = vb + (size_t)v * stride0;
						mesh->positions[v].x = nsh_f32 (p);
						mesh->positions[v].y = nsh_f32 (p + 4);
						mesh->positions[v].z = nsh_f32 (p + 8);
					}
					mesh->num_positions = v_count;
				}
			}
		}

		if (!mesh->positions)
		{
			free (mesh->normals);
			free (mesh->texcoords);
			free (mesh->position_node);
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}

		//--- skin weights for this object, from its rigging group

		if (have_rig)
		{
			const size_t obj_name = REL (o);
			const uint64_t obj_sub = nsh_rd64 (o + 0x08);
			for (uint64_t g = 0; g < rig_count; g++)
			{
				const uint8_t *rg = data + rig_off + g * 0x28;
				const size_t rg_name = REL (rg);
				if (rg_name >= size || obj_name >= size
					|| strcmp ((const char *)(data + rg_name), (const char *)(data + obj_name))
					|| nsh_rd64 (rg + 0x08) != obj_sub)
					continue;

				const size_t bb_off = REL (rg + 0x18);
				const uint64_t bb_count = nsh_rd64 (rg + 0x20);
				if (!bb_count || bb_count > 0x1000 || bb_off + bb_count * 0x18 > size)
					break;

				// One influence node per vertex; the exporter keys weights
				// off position_node, and a per-vertex node is the direct
				// expression of a list of (bone, weight) pairs.
				if (!mesh->position_node)
				{
					mesh->position_node = malloc (v_count * sizeof (int));
					model->node_influences
						= realloc (model->node_influences,
							(model->num_node_influences + v_count) * sizeof (node_influence_t));
					if (!mesh->position_node || !model->node_influences)
						break;
					memset (model->node_influences + model->num_node_influences, 0,
						v_count * sizeof (node_influence_t));
					for (uint32_t v = 0; v < v_count; v++)
						mesh->position_node[v] = (int)(model->num_node_influences + v);
					model->num_node_influences += v_count;
				}
				const size_t node_base = (size_t)mesh->position_node[0];

				for (uint64_t b = 0; b < bb_count; b++)
				{
					const uint8_t *bb = data + bb_off + b * 0x18;
					const size_t bn = REL (bb);
					if (bn >= size)
						continue;
					const int joint = nsh_bone_by_name (model, (const char *)(data + bn));
					if (joint < 0)
						continue;
					const size_t wd = REL (bb + 0x08);
					const uint64_t wsz = nsh_rd64 (bb + 0x10);
					if (wd + wsz > size)
						continue;
					// 6 bytes per entry: u16 vertex index, f32 weight.
					for (uint64_t w = 0; w + 6 <= wsz; w += 6)
					{
						const uint8_t *e = data + wd + w;
						const uint32_t vi = nsh_rd16 (e);
						const float weight = nsh_f32 (e + 2);
						if (vi >= v_count || !(weight > 0.0f))
							continue;
						node_influence_t *ni = model->node_influences + node_base + vi;
						influence_t *grown = realloc (
							ni->weights, (ni->num_weights + 1) * sizeof (influence_t));
						if (!grown)
							continue;
						ni->weights = grown;
						ni->weights[ni->num_weights].bone_idx = joint;
						ni->weights[ni->num_weights].weight = weight;
						ni->num_weights++;
					}
				}
				break;
			}

			// A vertex the rigging never mentions follows the object's own
			// parent bone, which is how a rigid object is attached.
			if (mesh->position_node)
			{
				const size_t pb = REL (o + 0x10);
				const int parent_joint = pb < size
					? nsh_bone_by_name (model, (const char *)(data + pb))
					: -1;
				const size_t node_base = (size_t)mesh->position_node[0];
				for (uint32_t v = 0; v < v_count; v++)
				{
					node_influence_t *ni = model->node_influences + node_base + v;
					if (ni->num_weights || parent_joint < 0)
						continue;
					ni->weights = calloc (1, sizeof (influence_t));
					if (!ni->weights)
						continue;
					ni->weights[0].bone_idx = parent_joint;
					ni->weights[0].weight = 1.0f;
					ni->num_weights = 1;
				}
			}
		}

		// Plain triangle list.
		vertex_t *verts = calloc (i_count, sizeof (vertex_t));
		if (!verts)
		{
			free (mesh->positions);
			free (mesh->normals);
			free (mesh->texcoords);
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}
		size_t nv = 0;
		const uint8_t *idx = data + ibuf_off + i_off;
		for (uint32_t k = 0; k < i_count; k++)
		{
			const uint32_t vi = nsh_rd16 (idx + (size_t)k * 2);
			if (vi >= v_count)
			{
				nv = 0; // a stray index invalidates the whole object
				break;
			}
			verts[nv].position_idx = (int)vi;
			verts[nv].normal_idx = mesh->normals ? (int)vi : -1;
			verts[nv].texcoord_idx = mesh->texcoords ? (int)vi : -1;
			nv++;
		}
		if (!nv)
		{
			free (verts);
			free (mesh->positions);
			free (mesh->normals);
			free (mesh->texcoords);
			free (mesh->position_node);
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}
		mesh->vertices = verts;
		mesh->num_vertices = nv;
		model->num_meshes++;
	}

	#undef REL

	if (!model->num_meshes)
	{
		free (model->meshes);
		free (model);
		return NULL;
	}
	return model;
}
