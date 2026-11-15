// SPDX-License-Identifier: GPL-2.0+
// Pikmin 1 .mod models, after the projectPiki/pikmin decompilation (CC0).
#include "lib-pikmin.h"
#include "lib-std.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef calloc
#undef realloc
#undef free

typedef struct { float m[12]; } mtx_t; // row-major 3x4

typedef struct
{
	int parent;
	vec3_t scale, rot, trans;
} pk_joint_t;

typedef struct
{
	const u8 *d;
	uint size;
	uint nvtx, nnrm, nnbt, ntex, njoint, nenv;
	const u8 *vtx, *nrm, *nbt, *tex;
	uint tex_off;
	pk_joint_t *joints;
	mtx_t *world;
	uint mesh_off, mesh_count; // body offset after padding
	uint joint_off;
} pk_t;

static float rf (const u8 *p) { union { u32 u; float f; } c; c.u = rd_be32 (p); return c.f; }
static uint rs (const u8 *p) { return p[0] << 8 | p[1]; }

static bool next_chunk (const u8 *d, uint size, uint *off, u32 *type, u32 *len)
{
	if (*off + 8 > size)
		return false;
	*type = rd_be32 (d + *off);
	*len = rd_be32 (d + *off + 4);
	return *len <= size - *off - 8;
}

bool IsPikminMod (const u8 *data, uint size)
{
	if (!data || size < 0x40 || rd_be32 (data) != 0 || rd_be32 (data + 4) < 0x20)
		return false;
	uint off = 0;
	u32 type, len;
	uint n = 0;
	while (next_chunk (data, size, &off, &type, &len))
	{
		if (type == 0xffff)
			return n >= 3;
		if (type != 0 && (type < 0x10 || type > 0x110))
			return false;
		off += 8 + len;
		off = (off + 0x1f) & ~0x1fu;
		n++;
	}
	return false;
}

static void mtx_ident (mtx_t *a)
{
	memset (a, 0, sizeof (*a));
	a->m[0] = a->m[5] = a->m[10] = 1;
}

static mtx_t mtx_mul (const mtx_t *a, const mtx_t *b)
{
	mtx_t r;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 4; j++)
			r.m[i * 4 + j] = a->m[i * 4] * b->m[j] + a->m[i * 4 + 1] * b->m[4 + j]
				+ a->m[i * 4 + 2] * b->m[8 + j] + (j == 3 ? a->m[i * 4 + 3] : 0);
	return r;
}

static mtx_t mtx_srt (const pk_joint_t *j)
{
	const float sx = sinf (j->rot.x), cx = cosf (j->rot.x);
	const float sy = sinf (j->rot.y), cy = cosf (j->rot.y);
	const float sz = sinf (j->rot.z), cz = cosf (j->rot.z);
	// R = Rz * Ry * Rx
	float r[3][3] = {
		{ cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx },
		{ sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx },
		{ -sy, cy * sx, cy * cx },
	};
	const float s[3] = { j->scale.x, j->scale.y, j->scale.z };
	mtx_t m;
	for (int i = 0; i < 3; i++)
		for (int k = 0; k < 3; k++)
			m.m[i * 4 + k] = r[i][k] * s[k];
	m.m[3] = j->trans.x;
	m.m[7] = j->trans.y;
	m.m[11] = j->trans.z;
	return m;
}

static void xform (const mtx_t *m, vec3_t *p, int w)
{
	const float x = p->x, y = p->y, z = p->z;
	p->x = m->m[0] * x + m->m[1] * y + m->m[2] * z + w * m->m[3];
	p->y = m->m[4] * x + m->m[5] * y + m->m[6] * z + w * m->m[7];
	p->z = m->m[8] * x + m->m[9] * y + m->m[10] * z + w * m->m[11];
}

static void compute_world (pk_t *pk)
{
	for (uint i = 0; i < pk->njoint; i++)
		pk->world[i].m[0] = NAN;
	for (uint i = 0; i < pk->njoint; i++)
	{
		int chain[256], n = 0, j = (int)i;
		while (j >= 0 && (uint)j < pk->njoint && isnan (pk->world[j].m[0]) && n < 256)
		{
			chain[n++] = j;
			j = pk->joints[j].parent;
		}
		mtx_t cur;
		if (j >= 0 && (uint)j < pk->njoint && !isnan (pk->world[j].m[0]))
			cur = pk->world[j];
		else
			mtx_ident (&cur);
		while (n--)
		{
			const mtx_t l = mtx_srt (pk->joints + chain[n]);
			cur = mtx_mul (&cur, &l);
			pk->world[chain[n]] = cur;
		}
	}
}

typedef struct { vec3_t *p, *n; vec2_t *t; size_t cnt, cap; } tri_acc_t;

static bool acc_push (tri_acc_t *a, vec3_t p, vec3_t n, vec2_t t)
{
	if (a->cnt == a->cap)
	{
		const size_t cap = a->cap ? a->cap * 2 : 1024;
		vec3_t *np = realloc (a->p, cap * sizeof (*np));
		if (!np) return false;
		a->p = np;
		vec3_t *nn = realloc (a->n, cap * sizeof (*nn));
		if (!nn) return false;
		a->n = nn;
		vec2_t *nt = realloc (a->t, cap * sizeof (*nt));
		if (!nt) return false;
		a->t = nt;
		a->cap = cap;
	}
	a->p[a->cnt] = p;
	a->n[a->cnt] = n;
	a->t[a->cnt] = t;
	a->cnt++;
	return true;
}

typedef struct { vec3_t p, n; vec2_t t; } pv_t;

static bool emit_tri (tri_acc_t *a, const pv_t *x, const pv_t *y, const pv_t *z)
{
	return acc_push (a, x->p, x->n, x->t) && acc_push (a, y->p, y->n, y->t) && acc_push (a, z->p, z->n, z->t);
}

// Decodes one display list into triangles. Returns false on malformed data.
static bool decode_dl (pk_t *pk, tri_acc_t *acc, const u8 *dl, uint len, uint flags, const int *dep, uint ndep,
	int parent)
{
	const uint ntexc = __builtin_popcount ((flags >> 3) & 0xff);
	const uint stride = 4 + ((flags & 4) ? 2 : 0) + 2 * ntexc;
	uint pos = 0;
	while (pos < len)
	{
		const uint op = dl[pos++];
		if (!op)
			break;
		if (pos + 2 > len)
			return false;
		const uint cnt = rs (dl + pos);
		pos += 2;
		const uint vsz = stride + ((flags & 1) ? 1 : 0) + ((flags & 2) ? 1 : 0);
		if (pos + (size_t)cnt * vsz > len)
			return false;
		pv_t *v = calloc (cnt ? cnt : 1, sizeof (*v));
		if (!v)
			return false;
		for (uint i = 0; i < cnt; i++)
		{
			const u8 *q = dl + pos + (size_t)i * vsz;
			int mtxid = -1;
			if (flags & 1)
				mtxid = *q++;
			if (flags & 2)
				q++;
			const uint pi = rs (q), ni = rs (q + 2);
			uint ti = 0;
			q += 4;
			if (flags & 4)
				q += 2;
			ti = rs (q);
			const bool nbt = (flags & 0x10000) != 0;
			if (pi >= pk->nvtx || (nbt ? (pk->nbt && ni >= pk->nnbt) : (pk->nrm && ni >= pk->nnrm)))
			{
				free (v);
				return false;
			}
			v[i].p = (vec3_t){ rf (pk->vtx + pi * 12), rf (pk->vtx + pi * 12 + 4), rf (pk->vtx + pi * 12 + 8) };
			const u8 *np = nbt ? (pk->nbt ? pk->nbt + ni * 36 : 0) : (pk->nrm ? pk->nrm + ni * 12 : 0);
			v[i].n = np ? (vec3_t){ rf (np), rf (np + 4), rf (np + 8) } : (vec3_t){ 0, 1, 0 };
			if (pk->tex && ti < pk->ntex)
				v[i].t = (vec2_t){ rf (pk->tex + ti * 8), rf (pk->tex + ti * 8 + 4) };
			int joint = parent;
			if (mtxid >= 0 && (uint)mtxid < ndep)
				joint = dep[mtxid];
			else if (mtxid >= 0 && mtxid % 3 == 0 && (uint)mtxid / 3 < ndep)
				joint = dep[mtxid / 3];
			if (joint >= 0 && (uint)joint < pk->njoint)
			{
				xform (pk->world + joint, &v[i].p, 1);
				xform (pk->world + joint, &v[i].n, 0);
			}
		}
		bool ok = true;
		switch (op)
		{
		case 0x80:
			for (uint i = 0; ok && i + 3 < cnt; i += 4)
				ok = emit_tri (acc, v + i, v + i + 1, v + i + 2) && emit_tri (acc, v + i, v + i + 2, v + i + 3);
			break;
		case 0x90:
			for (uint i = 0; ok && i + 2 < cnt; i += 3)
				ok = emit_tri (acc, v + i, v + i + 1, v + i + 2);
			break;
		case 0x98:
			for (uint i = 0; ok && i + 2 < cnt; i++)
				ok = (i & 1) ? emit_tri (acc, v + i + 1, v + i, v + i + 2) : emit_tri (acc, v + i, v + i + 1, v + i + 2);
			break;
		case 0xa0:
			for (uint i = 1; ok && i + 1 < cnt; i++)
				ok = emit_tri (acc, v, v + i, v + i + 1);
			break;
		default:
			break;
		}
		free (v);
		if (!ok)
			return false;
		pos += cnt * vsz;
	}
	return true;
}

static model_t *parse (const u8 *d, uint size)
{
	pk_t pk;
	memset (&pk, 0, sizeof (pk));
	pk.d = d;
	pk.size = size;
	uint off = 0;
	u32 type, len;
	uint mesh_chunk = 0, joint_chunk = 0;
	int have_env = 0;
	const u8 *env = 0;
	while (next_chunk (d, size, &off, &type, &len) && type != 0xffff)
	{
		const u8 *body = d + off + 8;
		const uint cnt = len >= 4 ? rd_be32 (body) : 0;
		const uint data_off = off + 0x20;
		const u8 *data = data_off <= size ? d + data_off : 0;
		const uint avail = data_off <= size ? size - data_off : 0;
		switch (type)
		{
		case 0x10:
			if ((size_t)cnt * 12 <= avail) { pk.nvtx = cnt; pk.vtx = data; }
			break;
		case 0x11:
			if (cnt && (size_t)cnt * 12 <= avail) { pk.nnrm = cnt; pk.nrm = data; }
			break;
		case 0x12:
			if (cnt && (size_t)cnt * 36 <= avail) { pk.nnbt = cnt; pk.nbt = data; }
			break;
		case 0x18:
			if ((size_t)cnt * 8 <= avail) { pk.ntex = cnt; pk.tex = data; }
			break;
		case 0x41:
			pk.nenv = cnt; env = data; have_env = 1;
			break;
		case 0x50:
			mesh_chunk = off; pk.mesh_count = cnt;
			break;
		case 0x60:
			joint_chunk = off; pk.njoint = cnt;
			break;
		}
		off += 8 + len;
		off = (off + 0x1f) & ~0x1fu;
	}
	(void)have_env; (void)env;
	if (!pk.vtx || !mesh_chunk || pk.mesh_count > 4096 || pk.njoint > 4096)
		return 0;

	pk.joints = calloc (pk.njoint ? pk.njoint : 1, sizeof (*pk.joints));
	pk.world = calloc (pk.njoint ? pk.njoint : 1, sizeof (*pk.world));
	if (!pk.joints || !pk.world)
		goto fail;
	if (joint_chunk)
	{
		uint p = joint_chunk + 0x20;
		for (uint i = 0; i < pk.njoint; i++)
		{
			if (p + 0x50 > size)
				goto fail;
			pk_joint_t *j = pk.joints + i;
			j->parent = (int)rd_be32 (d + p);
			j->scale = (vec3_t){ rf (d + p + 0x24), rf (d + p + 0x28), rf (d + p + 0x2c) };
			j->rot = (vec3_t){ rf (d + p + 0x30), rf (d + p + 0x34), rf (d + p + 0x38) };
			j->trans = (vec3_t){ rf (d + p + 0x3c), rf (d + p + 0x40), rf (d + p + 0x44) };
			const uint np = rd_be32 (d + p + 0x48);
			p += 0x4c + np * 4;
			if (np > 4096)
				goto fail;
		}
	}
	compute_world (&pk);

	model_t *model = calloc (1, sizeof (*model));
	if (!model)
		goto fail;
	uint p = mesh_chunk + 0x20;
	for (uint mi = 0; mi < pk.mesh_count; mi++)
	{
		if (p + 12 > size)
			break;
		const int parent = (int)rd_be32 (d + p);
		const uint flags = rd_be32 (d + p + 4);
		const uint ngroups = rd_be32 (d + p + 8);
		p += 12;
		if (ngroups > 4096)
			break;
		tri_acc_t acc;
		memset (&acc, 0, sizeof (acc));
		bool bad = false;
		for (uint g = 0; g < ngroups && !bad; g++)
		{
			if (p + 4 > size) { bad = true; break; }
			const uint ndep = rd_be32 (d + p);
			p += 4;
			if (ndep > 4096 || p + ndep * 2 > size) { bad = true; break; }
			int dep[4096];
			for (uint i = 0; i < ndep; i++)
				dep[i] = (int16_t)rs (d + p + i * 2);
			p += ndep * 2;
			if (p + 4 > size) { bad = true; break; }
			const uint ndl = rd_be32 (d + p);
			p += 4;
			for (uint k = 0; k < ndl; k++)
			{
				if (p + 12 > size) { bad = true; break; }
				const uint dlen = rd_be32 (d + p + 8);
				p += 12;
				p = (p + 0x1f) & ~0x1fu;
				if (dlen > size - p) { bad = true; break; }
				if (!decode_dl (&pk, &acc, d + p, dlen, flags, dep, ndep, parent))
					fprintf (stderr, "pikmin mod: display list %u/%u/%u malformed\n", mi, g, k);
				p += dlen;
			}
		}
		if (acc.cnt)
		{
			mesh_t *grown = realloc (model->meshes, (model->num_meshes + 1) * sizeof (*grown));
			if (!grown)
				bad = true;
			else
			{
				model->meshes = grown;
				mesh_t *m = grown + model->num_meshes++;
				memset (m, 0, sizeof (*m));
				snprintf (m->name, sizeof (m->name), "mesh%u", mi);
				m->positions = acc.p;
				m->num_positions = acc.cnt;
				m->normals = acc.n;
				m->num_normals = acc.cnt;
				m->texcoords = acc.t;
				m->num_texcoords = acc.cnt;
				m->vertices = calloc (acc.cnt, sizeof (vertex_t));
				m->num_vertices = acc.cnt;
				m->material_idx = -1;
				for (size_t i = 0; i < acc.cnt; i++)
				{
					vertex_t *v = m->vertices + i;
					v->position_idx = v->normal_idx = v->texcoord_idx = (int)i;
					v->tangent_idx = v->matrix_idx = -1;
					v->color_idx[0] = v->color_idx[1] = -1;
					for (int e = 0; e < 7; e++)
						v->extra_texcoord_idx[e] = -1;
				}
				memset (&acc, 0, sizeof (acc));
			}
		}
		free (acc.p); free (acc.n); free (acc.t);
		if (bad)
			break;
	}
	free (pk.joints);
	free (pk.world);
	if (!model->num_meshes)
	{
		FreeModel (model);
		return 0;
	}
	return model;
fail:
	free (pk.joints);
	free (pk.world);
	return 0;
}

enumError DecodePikminMod (const u8 *data, uint size, ccp out_path)
{
	if (!data || !out_path)
		return ERR_INVALID_DATA;
	model_t *model = parse (data, size);
	if (!model)
		return ERR_INVALID_DATA;
	const int rc = ExportModelToGLB (model, out_path);
	FreeModel (model);
	return rc == 0 ? ERR_OK : ERR_CANT_CREATE;
}
