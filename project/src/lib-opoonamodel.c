// SPDX-License-Identifier: GPL-2.0+
#include "lib-opoonamodel.h"
#include "lib-nintendo.h"
#include <math.h>
#include <stdlib.h>

// model_t members are released with plain free() by FreeModel()
#undef calloc
#undef malloc
#undef realloc
#undef free

// Chunk layout (offsets from the chunk start): u32 tag, u32 size, then
//   TMDM/BODM/SMDM: u32 flags (+8), u16/u32 count (+0xc), records from +0x10.
typedef struct opm_chunk_t
{
	const u8 *p;
	u32 size;
} opm_chunk_t;

static float opm_f (const u8 *p)
{
	u32 u = rd_be32 (p);
	float f;
	memcpy (&f, &u, sizeof (f));
	return f;
}

static bool opm_find (const u8 *d, size_t n, const char *tag, opm_chunk_t *out)
{
	size_t p = 0;
	while (p + 8 <= n)
	{
		const u32 sz = rd_be32 (d + p + 4);
		if (!memcmp (d + p, tag, 4))
		{
			if (sz > n - p - 8)
				return false;
			out->p = d + p;
			out->size = sz;
			return true;
		}
		if (!memcmp (d + p, "\0\0\0\0", 4) || sz > n - p - 8)
			break;
		p += 8 + (size_t)sz;
	}
	return false;
}

bool IsOpoonaModel (const u8 *d, size_t n)
{
	return n > 0x60 && !memcmp (d, "DHDM", 4);
}

// Rotation matrix of the node: q = qx(-ex) * qy(-ey) * qz(-ez), angles are s32
// with 65536 = 360 degrees (verified against the quaternions of the .mot clips).
static void opm_euler (const s32 e[3], float rot_deg[3])
{
	double a[3];
	for (int i = 0; i < 3; i++)
		a[i] = -(double)e[i] * (2.0 * M_PI / 65536.0);
	const double cx = cos (a[0]), sx = sin (a[0]), cy = cos (a[1]), sy = sin (a[1]), cz = cos (a[2]),
				 sz = sin (a[2]);
	// R = Rx * Ry * Rz
	const double R[3][3] = { { cy * cz, -cy * sz, sy },
		{ sx * sy * cz + cx * sz, -sx * sy * sz + cx * cz, -sx * cy },
		{ -cx * sy * cz + sx * sz, cx * sy * sz + sx * cz, cx * cy } };
	// decompose R = Rz(z) * Ry(y) * Rx(x)
	double x, y, z;
	const double c = sqrt (R[0][0] * R[0][0] + R[1][0] * R[1][0]);
	if (c > 1e-6)
	{
		y = atan2 (-R[2][0], c);
		z = atan2 (R[1][0], R[0][0]);
		x = atan2 (R[2][1], R[2][2]);
	}
	else
	{
		y = atan2 (-R[2][0], c);
		z = atan2 (-R[0][1], R[1][1]);
		x = 0;
	}
	rot_deg[0] = (float)(x * 180.0 / M_PI);
	rot_deg[1] = (float)(y * 180.0 / M_PI);
	rot_deg[2] = (float)(z * 180.0 / M_PI);
}

// Vertex groups: {u16 type, u16 count, count vertices}. Types 0x9b (triangle strip) and
// 0x93 (triangle list) carry skinned vertices of 28 + 4 * n_weights bytes {f32 pos[3],
// f32 normal[3], u16 u, u16 v (1/4096 units), f32 weight[n]} with n from the part header
// (u16 at +0x28, 0 = 1); types 0x9a (strip) and 0x92 (list) carry static map vertices of
// 36 bytes {f32 pos[3], f32 normal[3], u8 rgba[4], f32 u, f32 v}. Type 0 is padding.
static size_t opm_stride (uint ty, uint nw)
{
	if (ty == 0x9b || ty == 0x93)
		return 28 + 4 * (size_t)nw;
	if (ty == 0x9a || ty == 0x92)
		return 36;
	return 0;
}

static int opm_add_mesh_parts (model_t *model, const u8 *d, size_t n, size_t mo, size_t *mesh_cap,
	uint nnodes)
{
	if (mo + 0x30 > n)
		return 0;
	const u32 msize = rd_be32 (d + mo);
	const uint nparts = rd_be16 (d + mo + 8);
	if (msize > n - mo || nparts > 4096)
		return 0;
	size_t p = mo + 0x30;
	for (uint pi = 0; pi < nparts; pi++)
	{
		if (p + 0x30 > mo + msize)
			break;
		const u32 psize = rd_be32 (d + p + 0xc);
		if (psize < 0x30 || psize > mo + msize - p)
			break;
		const uint mat = rd_be16 (d + p + 6);
		uint nw = rd_be16 (d + p + 0x28);
		if (!nw)
			nw = 1;
		if (nw > 8)
		{
			p += psize;
			continue;
		}
		u16 bones[8];
		for (uint k = 0; k < 8; k++)
			bones[k] = rd_be16 (d + p + 0x10 + 2 * k);

		// first pass: counts
		size_t nverts = 0, nidx = 0;
		const size_t end = p + psize;
		for (size_t q = p + 0x30; q + 4 <= end;)
		{
			const uint ty = rd_be16 (d + q), cnt = rd_be16 (d + q + 2);
			const size_t stride = opm_stride (ty, nw);
			q += 4;
			if (!stride || q + cnt * stride > end)
				break;
			if (ty == 0x92 || ty == 0x93)
			{
				// triangle list
				nidx += cnt / 3 * 3;
				nverts += cnt;
			}
			else if (cnt >= 3)
				nidx += 3 * (size_t)(cnt - 2), nverts += cnt;
			q += cnt * stride;
		}
		if (!nverts)
		{
			p += psize;
			continue;
		}

		if (model->num_meshes >= *mesh_cap)
		{
			*mesh_cap = *mesh_cap ? *mesh_cap * 2 : 32;
			mesh_t *nm = realloc (model->meshes, *mesh_cap * sizeof (*nm));
			if (!nm)
				return -1;
			model->meshes = nm;
		}
		mesh_t *mesh = model->meshes + model->num_meshes;
		memset (mesh, 0, sizeof (*mesh));
		mesh->positions = calloc (nverts, sizeof (vec3_t));
		mesh->normals = calloc (nverts, sizeof (vec3_t));
		mesh->texcoords = calloc (nverts, sizeof (vec2_t));
		mesh->position_node = calloc (nverts, sizeof (int));
		mesh->vertices = calloc (nidx, sizeof (vertex_t));
		mesh->triangle_materials = calloc (nidx / 3 + 1, sizeof (int));
		if (!mesh->positions || !mesh->normals || !mesh->texcoords || !mesh->position_node
			|| !mesh->vertices || !mesh->triangle_materials)
			return -1;

		// influence entries for this part (skinned vertices only)
		node_influence_t *ni = realloc (model->node_influences,
			(model->num_node_influences + nverts) * sizeof (*ni));
		if (!ni)
			return -1;
		model->node_influences = ni;

		size_t vi = 0, ti = 0;
		for (size_t q = p + 0x30; q + 4 <= end;)
		{
			const uint ty = rd_be16 (d + q), cnt = rd_be16 (d + q + 2);
			const size_t stride = opm_stride (ty, nw);
			q += 4;
			if (!stride || q + cnt * stride > end)
				break;
			const bool is_list = ty == 0x92 || ty == 0x93;
			const bool skinned = ty == 0x9b || ty == 0x93;
			if (cnt >= 3)
			{
				const size_t base = vi;
				for (uint k = 0; k < cnt; k++, vi++)
				{
					const u8 *r = d + q + k * stride;
					mesh->positions[vi] = (vec3_t){ opm_f (r), opm_f (r + 4), opm_f (r + 8) };
					mesh->normals[vi] = (vec3_t){ opm_f (r + 12), opm_f (r + 16), opm_f (r + 20) };
					if (!skinned)
					{
						mesh->texcoords[vi] = (vec2_t){ opm_f (r + 28), opm_f (r + 32) };
						mesh->position_node[vi] = -1;
						continue;
					}
					mesh->texcoords[vi]
						= (vec2_t){ rd_be16 (r + 24) / 4096.0f, rd_be16 (r + 26) / 4096.0f };

					node_influence_t *inf = model->node_influences + model->num_node_influences;
					memset (inf, 0, sizeof (*inf));
					influence_t tmp[8];
					uint nn = 0;
					for (uint k2 = 0; k2 < nw; k2++)
					{
						const float w = nw == 1 ? 1.0f : opm_f (r + 28 + 4 * k2);
						if (w > 0.0f && bones[k2] < nnodes)
							tmp[nn++] = (influence_t){ bones[k2], w };
					}
					if (!nn)
						tmp[nn++] = (influence_t){ bones[0] < nnodes ? bones[0] : 0, 1.0f };
					inf->weights = malloc (nn * sizeof (influence_t));
					if (!inf->weights)
						return -1;
					memcpy (inf->weights, tmp, nn * sizeof (influence_t));
					inf->num_weights = nn;
					mesh->position_node[vi] = (int)model->num_node_influences++;
				}
				const uint ntri = is_list ? cnt / 3 : cnt - 2;
				for (uint t = 0; t < ntri; t++)
				{
					uint i3[3];
					if (is_list)
						i3[0] = 3 * t, i3[1] = 3 * t + 1, i3[2] = 3 * t + 2;
					else
						i3[0] = t & 1 ? t + 1 : t, i3[1] = t & 1 ? t : t + 1, i3[2] = t + 2;
					for (int j = 0; j < 3; j++)
					{
						vertex_t *v = mesh->vertices + ti * 3 + j;
						v->position_idx = v->normal_idx = v->texcoord_idx = (int)(base + i3[j]);
						v->tangent_idx = v->matrix_idx = -1;
						v->color_idx[0] = v->color_idx[1] = -1;
						for (int x = 0; x < 7; x++)
							v->extra_texcoord_idx[x] = -1;
					}
					ti++;
				}
			}
			q += cnt * stride;
		}
		mesh->num_positions = mesh->num_normals = mesh->num_texcoords = vi;
		mesh->num_vertices = ti * 3;
		mesh->material_idx = (int)mat;
		snprintf (mesh->name, sizeof (mesh->name), "part%zu_m%u", model->num_meshes, mat);
		model->num_meshes++;
		p += psize;
	}
	return 0;
}

model_t *ParseOpoonaModel (const u8 *d, size_t n, char tex_names[][64], uint ntex)
{
	if (!IsOpoonaModel (d, n))
		return 0;
	opm_chunk_t tmdm, bodm, smdm, tsdm;
	if (!opm_find (d, n, "BODM", &bodm) || !opm_find (d, n, "SMDM", &smdm)
		|| !opm_find (d, n, "TMDM", &tmdm))
		return 0;
	const uint nnodes = rd_be32 (bodm.p + 12);
	const uint nmats = rd_be16 (tmdm.p + 14);
	const uint nmesh = rd_be16 (smdm.p + 14);
	if (!nnodes || nnodes > 4096 || 16 + 0xc0ull * nnodes > bodm.size + 8
		|| 16 + 0x110ull * nmats > tmdm.size + 8 || 16 + 4ull * nmesh > smdm.size + 8)
		return 0;
	const bool has_tsdm = opm_find (d, n, "TSDM", &tsdm);

	model_t *model = calloc (1, sizeof (*model));
	if (!model)
		return 0;
	model->joints = calloc (nnodes, sizeof (joint_t));
	model->materials = calloc (nmats ? nmats : 1, sizeof (material_t));
	if (!model->joints || !model->materials)
		goto fail;
	model->num_joints = nnodes;
	model->num_materials = nmats;

	// nodes: first-child (+0xa0) / next-sibling (+0xa4), both 1-based; 0 = none
	int *parent = malloc (nnodes * sizeof (int));
	if (!parent)
		goto fail;
	for (uint i = 0; i < nnodes; i++)
		parent[i] = -1;
	for (uint i = 0; i < nnodes; i++)
	{
		const u8 *ob = bodm.p + 16 + 0xc0 * (size_t)i;
		const u32 child = rd_be32 (ob + 0xa0);
		// walk the sibling chain of the first child
		uint c = child, guard = 0;
		while (c && c <= nnodes && guard++ < nnodes)
		{
			parent[c - 1] = (int)i;
			c = rd_be32 (bodm.p + 16 + 0xc0 * (size_t)(c - 1) + 0xa4);
		}
	}
	for (uint i = 0; i < nnodes; i++)
	{
		const u8 *ob = bodm.p + 16 + 0xc0 * (size_t)i;
		joint_t *j = model->joints + i;
		const u32 noff = rd_be32 (ob + 0xb0);
		if (has_tsdm && noff < tsdm.size && memchr (tsdm.p + 8 + noff, 0, tsdm.size - noff))
			snprintf (j->name, sizeof (j->name), "%s", (ccp)tsdm.p + 8 + noff);
		else
			snprintf (j->name, sizeof (j->name), "node%u", i);
		j->parent_idx = parent[i];
		j->translate = (vec3_t){ opm_f (ob), opm_f (ob + 4), opm_f (ob + 8) };
		const s32 e[3] = { (s32)rd_be32 (ob + 0x10), (s32)rd_be32 (ob + 0x14), (s32)rd_be32 (ob + 0x18) };
		float r[3];
		opm_euler (e, r);
		j->rotate = (vec3_t){ r[0], r[1], r[2] };
		j->scale = (vec3_t){ opm_f (ob + 0x20), opm_f (ob + 0x24), opm_f (ob + 0x28) };
	}
	free (parent);
	ComputeModelTRSBinds (model);

	for (uint i = 0; i < nmats; i++)
	{
		const u8 *mb = tmdm.p + 16 + 0x110 * (size_t)i;
		material_t *m = model->materials + i;
		snprintf (m->name, sizeof (m->name), "mat%u", i);
		for (int k = 0; k < 4; k++)
			m->diffuse[k] = opm_f (mb + 0xc0 + 4 * k);
		const int tex = (s16)rd_be16 (mb + 0x104);
		if (tex >= 0 && (uint)tex < ntex && tex_names && tex_names[tex][0])
		{
			snprintf (m->textures[0], sizeof (m->textures[0]), "%s", tex_names[tex]);
			m->num_textures = 1;
			m->wrap_s[0] = m->wrap_t[0] = 1;
			m->min_filter[0] = m->mag_filter[0] = 1;
		}
		if (m->diffuse[3] < 1.0f)
			m->has_alpha = 1;
	}

	size_t mesh_cap = 0;
	for (uint i = 0; i < nmesh; i++)
	{
		const u32 mo = rd_be32 (smdm.p + 16 + 4 * (size_t)i);
		if (mo >= n)
			continue;
		if (opm_add_mesh_parts (model, d, n, mo, &mesh_cap, nnodes) < 0)
			goto fail;
	}
	if (!model->num_meshes)
		goto fail;
	return model;
fail:
	FreeModel (model);
	return 0;
}

// ".mot" clip: u32 0, f32 duration (frames), f32 fps (30), u32 node count (0x0c),
// ..., u32 footer offset (0x2c) = file size - 32; then one 80 byte record per node
// at 0x30 + 80 * k:
//   +0x00 u16 n_trans, u16 n_rot, u16 n_scale, u16 0, u32 n/a, u32 channel mask
//         (7 = translation, 0x38 = rotation, 0x1c0 = scale), u32, u32 rot_ptr (0x10)
//   +0x20 f32 bind_t[3], u32 trans_ptr (0x2c)
//   +0x30 f32 bind_q[4]
//   +0x40 f32 bind_s[3], u32 scale_ptr (0x4c)
// A key is 8 bytes: translation/scale = s16 x, y, z, u16 d -> value = s * d / 32768,
// rotation = s16 x, y, z, w / 4096 (a quaternion). Key i is at frame i, 30 fps.
// The tail of the file is a sorted {u32 name hash, u32 node} table.
static bool opm_chan (model_anim_channel_t *ch, const u8 *d, size_t n, u32 ptr, uint cnt, int node,
	model_anim_path_t path, float fps)
{
	if (!cnt || !ptr || ptr > n || (size_t)cnt * 8 > n - ptr)
		return false;
	const size_t comp = path == MODEL_ANIM_ROTATION ? 4 : 3;
	ch->times = malloc (cnt * sizeof (float));
	ch->values = malloc (cnt * comp * sizeof (float));
	if (!ch->times || !ch->values)
	{
		free (ch->times);
		free (ch->values);
		return false;
	}
	for (uint i = 0; i < cnt; i++)
	{
		const u8 *k = d + ptr + 8 * (size_t)i;
		ch->times[i] = i / fps;
		float *v = ch->values + comp * i;
		const float s[3] = { (s16)rd_be16 (k), (s16)rd_be16 (k + 2), (s16)rd_be16 (k + 4) };
		if (path == MODEL_ANIM_ROTATION)
		{
			float q[4] = { s[0] / 4096.0f, s[1] / 4096.0f, s[2] / 4096.0f, (s16)rd_be16 (k + 6) / 4096.0f };
			const float l = sqrtf (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
			for (int j = 0; j < 4; j++)
				v[j] = l > 1e-6f ? q[j] / l : (j == 3 ? 1.0f : 0.0f);
		}
		else
		{
			const float m = rd_be16 (k + 6) / 32768.0f;
			for (int j = 0; j < 3; j++)
				v[j] = s[j] * m;
		}
	}
	ch->node_idx = node;
	ch->path = path;
	ch->count = cnt;
	ch->components = comp;
	return true;
}

bool OpoonaAddMotion (model_t *model, const u8 *d, size_t n, const char *name)
{
	if (!model || !model->num_joints || n < 0x30 + 80 || rd_be32 (d + 0x0c) != model->num_joints)
		return false;
	const uint nb = model->num_joints;
	if (0x30 + 80ull * nb > n || rd_be32 (d + 0x2c) != n - 32)
		return false;
	float fps = opm_f (d + 8);
	if (!(fps > 1.0f && fps < 240.0f))
		fps = 30.0f;

	model_anim_channel_t *chs = calloc (3 * (size_t)nb, sizeof (*chs));
	if (!chs)
		return false;
	size_t nch = 0;
	for (uint k = 0; k < nb; k++)
	{
		const u8 *r = d + 0x30 + 80 * (size_t)k;
		if (opm_chan (chs + nch, d, n, rd_be32 (r + 0x2c), rd_be16 (r), (int)k, MODEL_ANIM_TRANSLATION, fps))
			nch++;
		if (opm_chan (chs + nch, d, n, rd_be32 (r + 0x10), rd_be16 (r + 2), (int)k, MODEL_ANIM_ROTATION, fps))
			nch++;
		if (opm_chan (chs + nch, d, n, rd_be32 (r + 0x4c), rd_be16 (r + 4), (int)k, MODEL_ANIM_SCALE, fps))
			nch++;
	}
	if (!nch)
	{
		free (chs);
		return false;
	}
	model_animation_t *na = realloc (model->animations, (model->num_animations + 1) * sizeof (*na));
	if (!na)
		return false;
	model->animations = na;
	model_animation_t *a = na + model->num_animations++;
	memset (a, 0, sizeof (*a));
	snprintf (a->name, sizeof (a->name), "%s", name);
	a->channels = chs;
	a->num_channels = nch;
	return true;
}
