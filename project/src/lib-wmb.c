// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// PlatinumGames WMB models (see lib-wmb.h).
//
// Validation philosophy matches the other model parsers here: every
// offset/count is bounds-checked up front, positions must be finite,
// normals must decode unit-length (10-10-10 packed), indices must land
// inside the vertex buffer, and meshes failing any check are skipped
// rather than emitted as garbage. Triangle strips become explicit
// triangles with alternating winding (as the reference rasterizes
// them backwards); degenerate triangles are dropped.
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
extern "C"
{
#endif
#include "types.h"
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-model-glb.h"
#include "lib-wmb.h"
#ifdef __cplusplus
}
#endif

#define WMB_MAX_MESHES 4096
#define WMB_MAX_BATCHES 4096
#define WMB_MAX_VERTS (16u << 20)
#define WMB_MAX_INDICES (64u << 20)
#define WMB_MAX_BONES 100000
#define WMB_MAX_MATERIALS 4096

// Verified (vertexFormat, numMapping, unknownD) layouts. stride covers
// the interleaved vertex; ex_size the extension block (colour, UV2).
typedef struct wmb_layout_t
{
	uint format;
	uint mapping;
	uint unk_d;
	uint stride;
	uint ex_size;
	bool bones;
	bool color;
	bool uv2;
} wmb_layout_t;

static const wmb_layout_t wmb_layouts[] = {
	{ 0x6b40001f, 1, 1, 32, 4, true, true, false },
	{ 0x6b40001f, 2, 1, 32, 8, true, true, true },
	{ 0x4b40000f, 1, 1, 28, 0, false, true, false },
	{ 0x4b40000f, 2, 1, 32, 0, false, true, true },
	{ 0x6b40001d, 1, 0, 32, 0, true, false, false },
	{ 0x4b40001f, 1, 1, 32, 4, true, true, false },
	{ 0x4b40001f, 2, 1, 32, 8, true, true, true },
};

static const wmb_layout_t *wmb_find_layout (uint format, uint mapping, uint unk_d)
{
	for (uint i = 0; i < sizeof (wmb_layouts) / sizeof (*wmb_layouts); i++)
		if (wmb_layouts[i].format == format && wmb_layouts[i].mapping == mapping
			&& wmb_layouts[i].unk_d == unk_d)
			return wmb_layouts + i;
	return 0;
}

static float wmb_f32be (const u8 *p)
{
	float v;
	u8 tmp[4];
	tmp[0] = p[3];
	tmp[1] = p[2];
	tmp[2] = p[1];
	tmp[3] = p[0];
	memcpy (&v, tmp, 4);
	return v;
}

static float wmb_halfbe (const u8 *p)
{
	const uint h = (uint)p[0] << 8 | p[1];
	const uint sign = h >> 15, exp = (h >> 10) & 31, mant = h & 1023;
	float v;
	if (!exp)
		v = mant / 16777216.0f;
	else if (exp == 31)
		v = mant ? 0.0f : 65504.0f;
	else
	{
		v = 1.0f + mant / 1024.0f;
		int e = (int)exp - 15;
		while (e > 0)
		{
			v *= 2;
			e--;
		}
		while (e < 0)
		{
			v *= .5f;
			e++;
		}
	}
	return sign ? -v : v;
}

// 10-10-10 packed normal (big-endian u32) to unit-checked xyz.
static bool wmb_normal101010 (float out[3], const u8 *p)
{
	const u32 r = (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
	int x = (int)(r & 0x3ff);
	int y = (int)((r >> 10) & 0x3ff);
	int z = (int)((r >> 20) & 0x3ff);
	if (x >= 512)
		x -= 1024;
	if (y >= 512)
		y -= 1024;
	if (z >= 512)
		z -= 1024;
	out[0] = (float)x / 511.0f;
	out[1] = (float)y / 511.0f;
	out[2] = (float)z / 511.0f;
	const float l = out[0] * out[0] + out[1] * out[1] + out[2] * out[2];
	return l > 0.81f && l < 1.21f;
}

typedef struct wmb_file_t
{
	const u8 *src;
	uint size;
	const wmb_layout_t *layout;
	uint num_verts;
	const u8 *verts;
	const u8 *verts_ex;
	uint num_bones;
	const u8 *bone_hie; // s16 parent per bone, -1 root
	const u8 *bone_rel; // float3 local translation per bone
	uint num_materials;
	const u8 *mat_ofs;  // u32 relative offset table
	const u8 *mat_base; // base address for materials
	const u8 *shd_base; // optional shader name table
	uint num_textures;
	const u8 *tex_base; // optional texture entry list (u32 hash, u32 type)
	uint num_meshes;
	const u8 *mesh_base;
	uint mesh_ofs_pos;
} wmb_file_t;

static enumError wmb_scan (wmb_file_t *m, const u8 *data, uint size)
{
	if (!m || !data || size < 0x80 || memcmp (data, "\0BMW", 4))
		return EINVAL;
	memset (m, 0, sizeof (*m));
	m->src = data;
	m->size = size;

	const uint format = rd_be32 (data + 8);
	const uint nv = rd_be32 (data + 12);
	if (!nv || nv > WMB_MAX_VERTS)
		return EINVAL;
	const uint mapping = data[0x10], unk_d = data[0x11];
	const wmb_layout_t *lay = wmb_find_layout (format, mapping, unk_d);
	if (!lay)
		return EINVAL;
	const uint op = rd_be32 (data + 0x14);
	const uint ov = rd_be32 (data + 0x18);
	const uint ove = rd_be32 (data + 0x1c);
	if (op > size || ov > size || ove > size)
		return EINVAL;
	if ((u64)ov + (u64)nv * lay->stride > size)
		return EINVAL;
	if (lay->ex_size && (u64)ove + (u64)nv * lay->ex_size > size)
		return EINVAL;
	const uint nbones = rd_be32 (data + 0x30);
	if (nbones > WMB_MAX_BONES)
		return EINVAL;
	// Bone tables must exist when claimed (positions validated on use).
	if (nbones)
	{
		const uint hie = rd_be32 (data + 0x34), da = rd_be32 (data + 0x38),
				   db = rd_be32 (data + 0x3c);
		if (!hie || !da || !db || (u64)hie + (u64)nbones * 2 > size
			|| (u64)da + (u64)nbones * 12 > size || (u64)db + (u64)nbones * 12 > size)
			return EINVAL;
		m->bone_hie = data + hie;
		m->bone_rel = data + da;
	}
	const uint nmesh = rd_be32 (data + 0x50);
	if (!nmesh || nmesh > WMB_MAX_MESHES)
		return EINVAL;
	const uint mofsp = rd_be32 (data + 0x54), mbase = rd_be32 (data + 0x58);
	if (mofsp > size || mbase > size || (u64)mofsp + (u64)nmesh * 4 > size)
		return EINVAL;
	for (uint i = 0; i < nmesh; i++)
	{
		const uint mo = rd_be32 (data + mofsp + i * 4);
		if ((u64)mbase + mo + 0x40 > size)
			return EINVAL;
	}

	const uint nmats = rd_be32 (data + 0x44);
	if (nmats > WMB_MAX_MATERIALS)
		return EINVAL;
	if (nmats)
	{
		const uint mat_ofs = rd_be32 (data + 0x48), mat_base = rd_be32 (data + 0x4c);
		if (mat_ofs > size || mat_base > size || (u64)mat_ofs + (u64)nmats * 4 > size)
			return EINVAL;
		for (uint i = 0; i < nmats; i++)
		{
			const uint ro = rd_be32 (data + mat_ofs + i * 4);
			if ((u64)mat_base + ro + 0x38 > size)
				return EINVAL;
		}
		m->num_materials = nmats;
		m->mat_ofs = data + mat_ofs;
		m->mat_base = data + mat_base;

		const uint shd_ofs = rd_be32 (data + 0x70);
		if (shd_ofs && (u64)shd_ofs + (u64)nmats * 16 <= size)
			m->shd_base = data + shd_ofs;
	}

	const uint tex_ofs = rd_be32 (data + 0x74);
	if (tex_ofs && tex_ofs + 4 <= size)
	{
		const uint ntex = rd_be32 (data + tex_ofs);
		if (ntex <= 4096 && (u64)tex_ofs + 4 + (u64)ntex * 8 <= size)
		{
			m->num_textures = ntex;
			m->tex_base = data + tex_ofs + 4;
		}
	}

	m->layout = lay;
	m->num_verts = nv;
	m->verts = data + ov;
	m->verts_ex = data + ove;
	m->num_bones = nbones;
	m->num_meshes = nmesh;
	m->mesh_base = data + mbase;
	m->mesh_ofs_pos = mofsp;
	return ERR_OK;
}

bool IsPlatinumWMB (const u8 *data, uint size)
{
	if (!data || size < 0x80)
		return false;
	wmb_file_t m;
	return wmb_scan (&m, data, size) == ERR_OK;
}

// First printable run in the 32-byte mesh name field (often NUL-padded).
static void wmb_mesh_name (char out[64], const u8 *raw)
{
	uint o = 0;
	while (o < 32 && (raw[o] < 32 || raw[o] > 126))
		o++;
	uint n = 0;
	while (o < 32 && n + 1 < 64 && raw[o] >= 32 && raw[o] <= 126)
		out[n++] = (char)raw[o++];
	out[n] = 0;
	if (!n)
		snprintf (out, 64, "mesh");
}

model_t *ParsePlatinumWMB (const u8 *data, size_t size)
{
	if (!data || !size || size > UINT_MAX)
		return 0;
	wmb_file_t m;
	if (wmb_scan (&m, data, (uint)size))
		return 0;
	const wmb_layout_t *lay = m.layout;

	model_t *model = CALLOC (1, sizeof (*model));
	if (!model)
		return 0;
	model->meshes = CALLOC (m.num_meshes * 4 + 1, sizeof (*model->meshes));
	if (!model->meshes)
	{
		FREE (model);
		return 0;
	}
	size_t mesh_cap = m.num_meshes * 4 + 1;

	// Skeleton: anonymous joints from the parent list + relative
	// translations (reference Model_Bayo_CreateBones). Binds derive
	// via ComputeModelTRSBinds once meshes exist; any failure below
	// degrades to unskinned geometry, never to no geometry.
	bool have_bones = false;
	if (m.num_bones)
	{
		model->joints = CALLOC (m.num_bones, sizeof (*model->joints));
		if (model->joints)
		{
			uint i = 0;
			for (; i < m.num_bones; i++)
			{
				const int parent = (int)(int16_t)rd_be16 (m.bone_hie + i * 2);
				if (parent < -1 || parent >= (int)m.num_bones)
					break;
				float tx = wmb_f32be (m.bone_rel + i * 12);
				float ty = wmb_f32be (m.bone_rel + i * 12 + 4);
				float tz = wmb_f32be (m.bone_rel + i * 12 + 8);
				if (!(tx > -1e9f && tx < 1e9f && ty > -1e9f && ty < 1e9f
						&& tz > -1e9f && tz < 1e9f))
					break;
				joint_t *j = model->joints + i;
				snprintf (j->name, sizeof (j->name), "bone%03u", i);
				j->parent_idx = parent;
				j->translate.x = tx;
				j->translate.y = ty;
				j->translate.z = tz;
				j->scale.x = j->scale.y = j->scale.z = 1.0f;
			}
			if (i == m.num_bones)
			{
				model->num_joints = m.num_bones;
				have_bones = true;
			}
			else
			{
				FREE (model->joints);
				model->joints = 0;
			}
		}
	}
	bool have_skin = false;

	// Materials: parse descriptors from material offset table
	if (m.num_materials)
	{
		model->materials = CALLOC (m.num_materials, sizeof (*model->materials));
		if (model->materials)
		{
			model->num_materials = m.num_materials;
			for (uint i = 0; i < m.num_materials; i++)
			{
				material_t *mat = model->materials + i;
				if (m.shd_base)
				{
					char shd[17];
					memcpy (shd, m.shd_base + i * 16, 16);
					shd[16] = 0;
					// Trim trailing spaces / non-printables
					for (int k = 15; k >= 0; k--)
					{
						if ((u8)shd[k] <= 32)
							shd[k] = 0;
						else
							break;
					}
					if (shd[0])
						snprintf (mat->name, sizeof (mat->name), "%s_%u", shd, i);
					else
						snprintf (mat->name, sizeof (mat->name), "mat%03u", i);
				}
				else
					snprintf (mat->name, sizeof (mat->name), "mat%03u", i);

				const uint ro = rd_be32 (m.mat_ofs + i * 4);
				const u8 *mp = m.mat_base + ro;

				// Slot 1 texture hash
				const u32 tex_hash = rd_be32 (mp + 4);
				if (tex_hash)
				{
					snprintf (mat->textures[0], sizeof (mat->textures[0]), "%08x", tex_hash);
					mat->num_textures = 1;
				}

				// Diffuse color from material floats (words 6..9)
				float r = wmb_f32be (mp + 24);
				float g = wmb_f32be (mp + 28);
				float b = wmb_f32be (mp + 32);
				float a = wmb_f32be (mp + 36);
				if (r >= 0.0f && r <= 1.0f && g >= 0.0f && g <= 1.0f && b >= 0.0f && b <= 1.0f)
				{
					mat->diffuse[0] = r;
					mat->diffuse[1] = g;
					mat->diffuse[2] = b;
					mat->diffuse[3] = (a >= 0.0f && a <= 1.0f) ? a : 1.0f;
				}
				else
				{
					mat->diffuse[0] = 0.8f;
					mat->diffuse[1] = 0.8f;
					mat->diffuse[2] = 0.8f;
					mat->diffuse[3] = 1.0f;
				}
			}
		}
	}

	for (uint mi = 0; mi < m.num_meshes; mi++)
	{
		const uint mo = rd_be32 (data + m.mesh_ofs_pos + mi * 4);
		const u8 *me = m.mesh_base + mo;
		const uint nbatches = rd_be16 (me + 2);
		const uint bofs = rd_be32 (me + 8);
		if (!nbatches || nbatches > WMB_MAX_BATCHES)
			continue;
		if ((u64)(me - data) + bofs + (u64)nbatches * 4 > size)
			continue;
		char mname[64];
		wmb_mesh_name (mname, me + 24);

		for (uint bi = 0; bi < nbatches; bi++)
		{
			const uint blist = rd_be32 (me + bofs + bi * 4);
			if ((u64)(me - data) + bofs + blist + 0x48 > size)
				continue;
			const u8 *b = me + bofs + blist;
			const uint vs = rd_be32 (b + 12), ve = rd_be32 (b + 16);
			const uint prim = rd_be32 (b + 20), ioff = rd_be32 (b + 24);
			const uint ni = rd_be32 (b + 28);
			// vertOfs == vertStart corpus-wide; indices address the batch.
			if (vs > ve || ve > m.num_verts || !ni || ni > WMB_MAX_INDICES)
				continue;
			if ((u64)(b - data) + ioff + (u64)ni * 2 > size)
				continue;
			const uint nremap = rd_be32 (b + 0x38);
			if (nremap > 1024 || (nremap && (u64)(b - data) + 0x3c + nremap > size))
				continue;
			// Remap values address real bones; raw indices address the
			// remap when present, the skeleton otherwise. Either way
			// they must stay inside the bone count for skinned layouts.
			if (lay->bones)
			{
				if (!m.num_bones)
					continue;
				bool bok = true;
				for (uint k = 0; k < nremap && bok; k++)
					if (b[0x3c + k] >= m.num_bones)
						bok = false;
				if (!bok)
					continue;
			}

			mesh_t *mesh = 0;
			if (model->num_meshes < mesh_cap)
				mesh = model->meshes + model->num_meshes;
			else
				continue;
			snprintf (mesh->name, sizeof (mesh->name), "%s_%u", mname, bi);
			const uint bmat = rd_be16 (b + 6);
			mesh->material_idx = (bmat < model->num_materials) ? (int)bmat : -1;
			const uint vc = ve - vs;
			if (!vc)
				continue;
			mesh->positions = CALLOC (vc, sizeof (*mesh->positions));
			mesh->normals = CALLOC (vc, sizeof (*mesh->normals));
			mesh->texcoords = CALLOC (vc, sizeof (*mesh->texcoords));
			if (!mesh->positions || !mesh->normals || !mesh->texcoords)
			{
				FREE (mesh->positions);
				mesh->positions = 0;
				FREE (mesh->normals);
				mesh->normals = 0;
				FREE (mesh->texcoords);
				mesh->texcoords = 0;
				continue;
			}
			bool ok = true;
			bool have_uv = true;
			for (uint v = 0; v < vc && ok; v++)
			{
				const u8 *vp = m.verts + (size_t)(vs + v) * lay->stride;
				float x = wmb_f32be (vp), y = wmb_f32be (vp + 4), z = wmb_f32be (vp + 8);
				if (!(x > -1e9f && x < 1e9f && y > -1e9f && y < 1e9f && z > -1e9f && z < 1e9f))
				{
					ok = false;
					break;
				}
				mesh->positions[v].x = x;
				mesh->positions[v].y = y;
				mesh->positions[v].z = z;
				float nrm[3];
				if (!wmb_normal101010 (nrm, vp + 16))
				{
					ok = false;
					break;
				}
				mesh->normals[v].x = nrm[0];
				mesh->normals[v].y = nrm[1];
				mesh->normals[v].z = nrm[2];
				if (have_uv)
				{
					mesh->texcoords[v].u = wmb_halfbe (vp + 12);
					mesh->texcoords[v].v = wmb_halfbe (vp + 14);
					if (!(mesh->texcoords[v].u > -1e4f && mesh->texcoords[v].u < 1e4f
							&& mesh->texcoords[v].v > -1e4f && mesh->texcoords[v].v < 1e4f))
						have_uv = false;
				}
				if (lay->bones)
				{
					// Skinning is validated, not exported (no skeleton
					// oracle here): raw indices address the remap table
					// when one is present, the skeleton otherwise.
					const u8 *bp = vp + 24;
					for (uint k = 0; k < 4; k++)
						if (bp[k] >= (nremap ? nremap : m.num_bones))
						{
							ok = false;
							break;
						}
					if (!ok)
						break;
				}
			}
			if (!ok)
			{
				FREE (mesh->positions);
				mesh->positions = 0;
				FREE (mesh->normals);
				mesh->normals = 0;
				FREE (mesh->texcoords);
				mesh->texcoords = 0;
				memset (mesh, 0, sizeof (*mesh));
				continue;
			}
			mesh->num_positions = vc;
			mesh->num_normals = vc;
			if (have_uv)
				mesh->num_texcoords = vc;
			else
			{
				FREE (mesh->texcoords);
				mesh->texcoords = 0;
			}

			// Indices: u16; primType 4 is a plain list, anything else
			// a triangle strip (alternating winding).
			const uint ntri = prim == 4 ? ni / 3 : ni >= 3 ? ni - 2 : 0;
			if (!ntri)
			{
				FREE (mesh->positions);
				mesh->positions = 0;
				FREE (mesh->normals);
				mesh->normals = 0;
				FREE (mesh->texcoords);
				mesh->texcoords = 0;
				memset (mesh, 0, sizeof (*mesh));
				continue;
			}
			vertex_t *verts = CALLOC ((size_t)ntri * 3, sizeof (*verts));
			if (!verts)
			{
				FREE (mesh->positions);
				mesh->positions = 0;
				FREE (mesh->normals);
				mesh->normals = 0;
				FREE (mesh->texcoords);
				mesh->texcoords = 0;
				memset (mesh, 0, sizeof (*mesh));
				continue;
			}
			size_t nv = 0;
			const u8 *idx = b + ioff;
			for (uint t = 0; t < ntri; t++)
			{
				uint a, b2, c;
				if (prim == 4)
				{
					a = rd_be16 (idx + (size_t)t * 6);
					b2 = rd_be16 (idx + (size_t)t * 6 + 2);
					c = rd_be16 (idx + (size_t)t * 6 + 4);
				}
				else
				{
					a = rd_be16 (idx + (size_t)t * 2);
					b2 = rd_be16 (idx + (size_t)t * 2 + 2);
					c = rd_be16 (idx + (size_t)t * 2 + 4);
					if (t & 1)
					{
						const uint tmp = b2;
						b2 = c;
						c = tmp;
					}
				}
				// Stored backward-wound (the reference rasterizes with
				// reversed winding); flip every triangle so faces agree
				// with the vertex normals under standard convention
				// (proven 0/1618 -> 1618/1618 on retail Fox McCloud).
				{
					const uint tmp = b2;
					b2 = c;
					c = tmp;
				}
				// Batch indices are window-relative (absolute vertex
				// is vertStart + index); the window was copied above,
				// so emit relative indices directly.
				if (a >= vc || b2 >= vc || c >= vc)
				{
					nv = 0;
					break;
				}
				// Lists drop degenerate triangles; strips keep them:
				// repeated indices are restart/stitch markers, and
				// dropping them would break the multiple-of-3 count.
				if (prim == 4 && (a == b2 || b2 == c || a == c))
					continue;
				const uint tri[3] = { a, b2, c };
				for (uint k = 0; k < 3; k++)
				{
					verts[nv].position_idx = (int)tri[k];
					verts[nv].normal_idx = (int)tri[k];
					verts[nv].tangent_idx = -1;
					verts[nv].texcoord_idx = mesh->texcoords ? (int)tri[k] : -1;
					verts[nv].matrix_idx = -1;
					verts[nv].color_idx[0] = verts[nv].color_idx[1] = -1;
					nv++;
				}
			}
			if (!nv)
			{
				FREE (verts);
				FREE (mesh->positions);
				mesh->positions = 0;
				FREE (mesh->normals);
				mesh->normals = 0;
				FREE (mesh->texcoords);
				mesh->texcoords = 0;
				memset (mesh, 0, sizeof (*mesh));
				continue;
			}
			mesh->vertices = verts;
			mesh->num_vertices = nv;
			// Skin influences from the vertex buffer (raw index remapped
			// per batch, weights are u8/255). Zero-sum rows bind rigidly
			// to joint 0 like the reference's unbound-vertex default.
			if (have_bones && lay->bones)
			{
				mesh->position_node = CALLOC (vc, sizeof (*mesh->position_node));
				if (mesh->position_node)
				{
					bool weights_ok = true;
					for (uint v = 0; v < vc && weights_ok; v++)
					{
						mesh->position_node[v] =
							(int)(model->num_node_influences + v);
					}
					node_influence_t *grown = REALLOC (model->node_influences,
						(model->num_node_influences + vc) * sizeof (*grown));
					if (!grown)
						weights_ok = false;
					else
					{
						model->node_influences = grown;
						memset (model->node_influences + model->num_node_influences, 0,
							vc * sizeof (*grown));
						for (uint v = 0; v < vc && weights_ok; v++)
						{
							const u8 *bp = m.verts + (size_t)(vs + v) * lay->stride + 24;
							const u8 *wp = m.verts + (size_t)(vs + v) * lay->stride + 28;
							node_influence_t *ni = model->node_influences
								+ model->num_node_influences + v;
							float wsum = 0;
							for (uint k = 0; k < 4; k++)
							{
								uint raw = bp[k];
								if (raw >= (nremap ? nremap : m.num_bones))
								{
									weights_ok = false;
									break;
								}
								const uint joint =
									nremap ? b[0x3c + raw] : raw;
								if (joint >= m.num_bones)
								{
									weights_ok = false;
									break;
								}
								const float w = wp[k] / 255.0f;
								wsum += w;
								if (w <= 0.0f)
									continue;
								influence_t *ig = REALLOC (ni->weights,
									(ni->num_weights + 1) * sizeof (*ig));
								if (!ig)
								{
									weights_ok = false;
									break;
								}
								ni->weights = ig;
								ni->weights[ni->num_weights].bone_idx = (int)joint;
								ni->weights[ni->num_weights].weight = w;
								ni->num_weights++;
							}
							if (weights_ok && wsum <= 0.0f)
							{
								influence_t *ig = REALLOC (ni->weights, sizeof (*ig));
								if (!ig)
									weights_ok = false;
								else
								{
									ni->weights = ig;
									ni->weights[0].bone_idx = 0;
									ni->weights[0].weight = 1.0f;
									ni->num_weights = 1;
								}
							}
						}
						if (weights_ok)
						{
							model->num_node_influences += vc;
							have_skin = true;
						}
					}
					if (!weights_ok)
					{
						// Leave the geometry; drop this mesh's skin data.
						for (uint v = 0; v < vc; v++)
						{
							node_influence_t *ni = model->node_influences
								+ model->num_node_influences + v;
							FREE (ni->weights);
							ni->weights = 0;
							ni->num_weights = 0;
						}
						FREE (mesh->position_node);
						mesh->position_node = 0;
					}
				}
			}
			model->num_meshes++;
		}
	}

	if (!model->num_meshes)
	{
		FreeModel (model);
		return 0;
	}
	if (have_skin && !ComputeModelTRSBinds (model))
	{
		// Bad bind chain: keep the geometry, drop the skinning.
		for (size_t i = 0; i < model->num_meshes; i++)
		{
			FREE (model->meshes[i].position_node);
			model->meshes[i].position_node = 0;
		}
		for (size_t i = 0; i < model->num_node_influences; i++)
			FREE (model->node_influences[i].weights);
		FREE (model->node_influences);
		model->node_influences = 0;
		model->num_node_influences = 0;
		FREE (model->joints);
		model->joints = 0;
		model->num_joints = 0;
	}
	else if (!have_skin)
	{
		FREE (model->joints);
		model->joints = 0;
		model->num_joints = 0;
	}
	return model;
}
