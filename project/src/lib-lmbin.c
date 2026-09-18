// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-lmbin.h"
#include "lib-model-glb.h"
#include "lib-excite.h"
#include "lib-image.h"
#include <string.h>
#include <math.h>

// GX attribute bits (matches the reference's GXAttributes checks:
// Normal=bit10, Color0=bit11, Color1=bit12, TexN=bit13+N).
#define LMB_GX_NRM (1u << 10)
#define LMB_GX_C0 (1u << 11)
#define LMB_GX_C1 (1u << 12)
#define LMB_GX_TX(n) (1u << (13 + (n)))

#define LMB_MAX_SECT 8192
#define LMB_MAX_VERTS (8u << 20)

static inline u16 lmb_be16 (const u8 *p)
{
	return (u16)((u16)p[0] << 8 | p[1]);
}

static inline s16 lmb_be16s (const u8 *p)
{
	return (s16)lmb_be16 (p);
}

static inline u32 lmb_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline float lmb_bef32 (const u8 *p)
{
	u32 u = lmb_be32 (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

static inline void lmb_wr16 (u8 *p, u16 v)
{
	p[0] = (u8)(v >> 8);
	p[1] = (u8)v;
}

static inline void lmb_wr32 (u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

static inline void lmb_wrf32 (u8 *p, float f)
{
	u32 u;
	memcpy (&u, &f, 4);
	lmb_wr32 (p, u);
}

typedef struct
{
	u32 tex, samp, pos, nrm, at1, at2, uv, at3, at4, at5, mat, batch, graph;
} lmb_off_t;

static bool lmb_read_hdr (const u8 *data, uint size, lmb_off_t *o)
{
	if (!data || size < 64 || data[0] != 2)
		return false;
	o->tex = lmb_be32 (data + 12);
	o->samp = lmb_be32 (data + 16);
	o->pos = lmb_be32 (data + 20);
	o->nrm = lmb_be32 (data + 24);
	o->at1 = lmb_be32 (data + 28);
	o->at2 = lmb_be32 (data + 32);
	o->uv = lmb_be32 (data + 36);
	o->at3 = lmb_be32 (data + 40);
	o->at4 = lmb_be32 (data + 44);
	o->at5 = lmb_be32 (data + 48);
	o->mat = lmb_be32 (data + 52);
	o->batch = lmb_be32 (data + 56);
	o->graph = lmb_be32 (data + 60);
	// every section must start inside the file, in header order
	const u32 arr[] = { o->tex, o->samp, o->pos, o->nrm, o->at1, o->at2, o->uv, o->at3,
		o->at4, o->at5, o->mat, o->batch, o->graph };
	for (uint i = 0; i < 13; i++)
		if (!arr[i] || arr[i] >= size || (i && arr[i] < arr[i - 1]))
			return false;
	if (o->graph + 140 > size)
		return false;
	return true;
}

bool IsLMBIN (const u8 *data, size_t size)
{
	lmb_off_t o;
	return size <= 0xffffffffu && lmb_read_hdr (data, (uint)size, &o);
}

// GX tiled size (raw GX enum: I4=0 .. CMPR=14).
static uint lmb_gx_size (uint gx, uint w, uint h)
{
	uint bw = 4, bh = 4, bpp = 16;
	switch (gx)
	{
		case 0:
			bw = 8;
			bh = 8;
			bpp = 4;
			break;
		case 1:
		case 2:
			bw = 8;
			bh = 4;
			bpp = 8;
			break;
		case 3:
		case 4:
		case 5:
			bw = 4;
			bh = 4;
			bpp = 16;
			break;
		case 6:
			bw = 4;
			bh = 4;
			bpp = 32;
			break;
		case 14:
			bw = 8;
			bh = 8;
			bpp = 4;
			break;
		default:
			return 0;
	}
	return ((w + bw - 1) / bw) * ((h + bh - 1) / bh) * (bw * bh * bpp / 8);
}

// Compose a ZYX-degree TRS matrix (row-major 3x4), same convention as the
// LM MDL importer.
static void lmb_trs (float out[12], const float s[3], const float rdeg[3], const float t[3])
{
	const double dx = rdeg[0] * (M_PI / 180.0), dy = rdeg[1] * (M_PI / 180.0),
				 dz = rdeg[2] * (M_PI / 180.0);
	const float cx = cosf ((float)dx), sx = sinf ((float)dx), cy = cosf ((float)dy),
				sy = sinf ((float)dy), cz = cosf ((float)dz), sz = sinf ((float)dz);
	float rot[9] = { cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx, sz * cy,
		sz * sy * sx + cz * cx, sz * sy * cx - cz * sx, -sy, cy * sx, cy * cx };
	for (int r = 0; r < 3; r++)
	{
		out[r * 4] = rot[r * 3] * s[0];
		out[r * 4 + 1] = rot[r * 3 + 1] * s[1];
		out[r * 4 + 2] = rot[r * 3 + 2] * s[2];
	}
	out[3] = t[0];
	out[7] = t[1];
	out[11] = t[2];
}

static void lmb_mul43 (float out[12], const float a[12], const float b[12])
{
	float t[12];
	for (int r = 0; r < 3; r++)
	{
		for (int c = 0; c < 3; c++)
			t[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c];
		t[r * 4 + 3] = a[r * 4] * b[3] + a[r * 4 + 1] * b[7] + a[r * 4 + 2] * b[11] + a[r * 4 + 3];
	}
	memcpy (out, t, sizeof (t));
}

static void lmb_xpos (const float m[12], float *x, float *y, float *z)
{
	const float px = *x, py = *y, pz = *z;
	*x = m[0] * px + m[1] * py + m[2] * pz + m[3];
	*y = m[4] * px + m[5] * py + m[6] * pz + m[7];
	*z = m[8] * px + m[9] * py + m[10] * pz + m[11];
}

static void lmb_xnrm (const float m[12], float *x, float *y, float *z)
{
	const float px = *x, py = *y, pz = *z;
	*x = m[0] * px + m[1] * py + m[2] * pz;
	*y = m[4] * px + m[5] * py + m[6] * pz;
	*z = m[8] * px + m[9] * py + m[10] * pz;
}

typedef struct
{
	int pos, nrm, uv;
} lmb_corner_t;

typedef struct
{
	lmb_corner_t *v;
	size_t num, cap;
} lmb_soup_t;

static bool lmb_push (lmb_soup_t *s, lmb_corner_t c)
{
	if (s->num >= s->cap)
	{
		const size_t nc = s->cap ? s->cap * 2 : 256;
		lmb_corner_t *nn = REALLOC (s->v, nc * sizeof (*nn));
		if (!nn)
			return false;
		s->v = nn;
		s->cap = nc;
	}
	s->v[s->num++] = c;
	return true;
}

static bool lmb_tris (lmb_soup_t *out, u8 op, const lmb_corner_t *v, uint n)
{
	if (op == 0x90)
	{
		for (uint i = 0; i + 2 < n; i += 3)
			if (!lmb_push (out, v[i]) || !lmb_push (out, v[i + 1]) || !lmb_push (out, v[i + 2]))
				return false;
		return true;
	}
	if (n < 3)
		return true;
	if (op == 0xa0)
	{
		for (uint i = 0; i < 3 && i < n; i++)
			if (!lmb_push (out, v[i]))
				return false;
		for (uint i = 3; i < n; i++)
		{
			const lmb_corner_t a = v[0], b = v[i - 1], d = v[i];
			if ((a.pos != b.pos || a.uv != b.uv) && (b.pos != d.pos || b.uv != d.uv)
				&& (d.pos != a.pos || d.uv != a.uv))
				if (!lmb_push (out, b) || !lmb_push (out, d) || !lmb_push (out, a))
					return false;
		}
		return true;
	}
	for (uint i = 2; i < n; i++)
	{
		lmb_corner_t v0 = ((i % 2) == 0) ? v[i - 2] : v[i - 1];
		lmb_corner_t v1 = ((i % 2) == 0) ? v[i] : v[i - 2];
		lmb_corner_t v2 = ((i % 2) == 0) ? v[i - 1] : v[i];
		if ((v0.pos != v1.pos || v0.uv != v1.uv) && (v1.pos != v2.pos || v1.uv != v2.uv)
			&& (v2.pos != v0.pos || v2.uv != v0.uv))
			if (!lmb_push (out, v1) || !lmb_push (out, v2) || !lmb_push (out, v0))
				return false;
	}
	return true;
}

// Parse one shape-batch's packets. POOLS gives absolute offsets; returns
// false on any structural problem.
static bool lmb_walk_batch (const u8 *data, uint size, const lmb_off_t *o, uint bidx,
	lmb_soup_t *out, bool *has_nrm, bool *has_uv)
{
	const u64 bo = (u64)o->batch + (u64)bidx * 24;
	if (bo + 24 > size)
		return false;
	const u8 *bp = data + bo;
	const uint dlsz = lmb_be16 (bp + 2);
	const u32 attr = lmb_be32 (bp + 4);
	const uint ddoff = lmb_be32 (bp + 12);
	const bool hn = (attr & LMB_GX_NRM) != 0;
	const uint nuv = (bp[10] > 8) ? 8 : bp[10];
	if (hn)
		*has_nrm = true;
	if (nuv)
		*has_uv = true;
	const u64 base = (u64)o->batch + ddoff;
	const u64 end = base + (u64)dlsz * 0x20;
	if (ddoff > size || end > size || end < base)
		return false;
	const u8 *p = data + base, *stop = data + end;
	while (p < stop)
	{
		const u8 opc = *p++;
		if (opc == 0)
			continue;
		if (opc != 0x90 && opc != 0x98 && opc != 0xa0)
			return false;
		if (p + 2 > stop)
			return false;
		const uint n = (uint)p[0] << 8 | p[1];
		p += 2;
		if (!n || n > 65536)
			return false;
		lmb_corner_t *v = MALLOC (n * sizeof (*v));
		if (!v)
			return false;
		bool ok = true;
		for (uint i = 0; i < n && ok; i++)
		{
			uint need = 2;
			if (hn)
				need += 2;
			if (bp[11] /*NbtFlag*/)
				need += 4;
			if (attr & LMB_GX_C0)
				need += 2;
			if (attr & LMB_GX_C1)
				need += 2;
			for (uint t = 0; t < nuv; t++)
				if (attr & LMB_GX_TX (t))
					need += 2;
			if ((size_t)(stop - p) < need)
			{
				ok = false;
				break;
			}
			v[i].pos = lmb_be16s (p);
			p += 2;
			v[i].nrm = -1;
			v[i].uv = -1;
			if (hn)
			{
				v[i].nrm = lmb_be16s (p);
				p += 2;
				if (bp[11])
					p += 4;
			}
			if (attr & LMB_GX_C0)
				p += 2;
			if (attr & LMB_GX_C1)
				p += 2;
			for (uint t = 0; t < nuv; t++)
				if (attr & LMB_GX_TX (t))
				{
					const int uv = lmb_be16s (p);
					p += 2;
					if (!t)
						v[i].uv = uv;
				}
		}
		if (ok)
			ok = lmb_tris (out, opc, v, n);
		FREE (v);
		if (!ok)
			return false;
	}
	return true;
}

typedef struct
{
	int node;
	float world[12];
} lmb_node_t;

model_t *ParseLMBIN (const u8 *data, size_t size)
{
	lmb_off_t o;
	if (size > 0xffffffffu || !lmb_read_hdr (data, (uint)size, &o))
		return 0;

	//--- walk the scene graph from node 0 ---
	lmb_node_t *nodes = 0;
	size_t nnodes = 0, cap_nodes = 0;
	{
		uint stack[8192];
		size_t nst = 0;
		bool *seen = CALLOC (LMB_MAX_SECT, sizeof (*seen));
		if (!seen)
			return 0;
		stack[nst++] = 0;
		float identity[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
		while (nst)
		{
			const uint idx = stack[--nst];
			if (idx >= LMB_MAX_SECT || seen[idx])
				continue;
			const u64 no = (u64)o.graph + (u64)idx * 140;
			if (no + 140 > size)
			{
				FREE (seen);
				FREE (nodes);
				return 0;
			}
			seen[idx] = true;
			if (nnodes >= cap_nodes)
			{
				const size_t nc = cap_nodes ? cap_nodes * 2 : 64;
				lmb_node_t *nn = REALLOC (nodes, nc * sizeof (*nn));
				if (!nn)
				{
					FREE (seen);
					FREE (nodes);
					return 0;
				}
				nodes = nn;
				cap_nodes = nc;
			}
			const u8 *np = data + no;
			float s[3] = { lmb_bef32 (np + 12), lmb_bef32 (np + 16), lmb_bef32 (np + 20) };
			float r[3] = { lmb_bef32 (np + 24), lmb_bef32 (np + 28), lmb_bef32 (np + 32) };
			float t[3] = { lmb_bef32 (np + 36), lmb_bef32 (np + 40), lmb_bef32 (np + 44) };
			for (int k = 0; k < 3; k++)
				if (!isfinite (s[k]) || !isfinite (r[k]) || !isfinite (t[k]))
				{
					FREE (seen);
					FREE (nodes);
					return 0;
				}
			float local[12], world[12];
			lmb_trs (local, s, r, t);
			const int par = lmb_be16s (np);
			if (par < 0)
				memcpy (world, local, sizeof (world));
			else
			{
				// parent must already be resolved (graph is parent-ordered:
				// children always have higher indices in real files, but do
				// not rely on it -- resolve by index lookup)
				bool found = false;
				for (size_t k = 0; k < nnodes; k++)
					if (nodes[k].node == par)
					{
						lmb_mul43 (world, nodes[k].world, local);
						found = true;
						break;
					}
				if (!found)
					memcpy (world, local, sizeof (world));
			}
			nodes[nnodes].node = (int)idx;
			memcpy (nodes[nnodes].world, world, sizeof (world));
			nnodes++;
			const int fc = lmb_be16s (np + 2), ns = lmb_be16s (np + 4);
			if (fc >= 0 && nst + 2 < 8192)
			{
				stack[nst++] = (uint)fc;
				if (ns >= 0)
					stack[nst++] = (uint)ns;
			}
			else if (fc >= 0 || ns >= 0)
			{
				FREE (seen);
				FREE (nodes);
				return 0;
			}
			(void)identity;
		}
		FREE (seen);
	}
	if (!nnodes)
	{
		FREE (nodes);
		return 0;
	}

	model_t *model = CALLOC (1, sizeof (*model));
	if (!model)
	{
		FREE (nodes);
		return 0;
	}
	model->num_joints = nnodes;
	model->joints = CALLOC (nnodes, sizeof (*model->joints));
	if (!model->joints)
	{
		FREE (nodes);
		FreeModel (model);
		return 0;
	}
	// file-order index -> joint index
	for (size_t j = 0; j < nnodes; j++)
	{
		joint_t *jt = model->joints + j;
		snprintf (jt->name, sizeof (jt->name), "Node%u", (uint)nodes[j].node);
		const u8 *np = data + (u64)o.graph + (u64)nodes[j].node * 140;
		const int par = lmb_be16s (np);
		jt->parent_idx = -1;
		if (par >= 0)
			for (size_t k = 0; k < nnodes; k++)
				if (nodes[k].node == par)
				{
					jt->parent_idx = (int)k;
					break;
				}
		memcpy (jt->bind, nodes[j].world, sizeof (jt->bind));
		jt->has_inverse_bind = 0; // unskinned room geometry: no skin
		jt->translate.x = np ? lmb_bef32 (np + 36) : 0;
		jt->translate.y = lmb_bef32 (np + 40);
		jt->translate.z = lmb_bef32 (np + 44);
		jt->rotate.x = lmb_bef32 (np + 24);
		jt->rotate.y = lmb_bef32 (np + 28);
		jt->rotate.z = lmb_bef32 (np + 32);
		jt->scale.x = lmb_bef32 (np + 12);
		jt->scale.y = lmb_bef32 (np + 16);
		jt->scale.z = lmb_bef32 (np + 20);
	}
	// rigid single-bone influences so the skeleton survives a GLB round
	// trip (positions are baked with the joint worlds, hence the matching
	// inverse binds, exactly like the LM MDL importer).
	model->num_node_influences = nnodes;
	model->node_influences = CALLOC (nnodes ? nnodes : 1, sizeof (*model->node_influences));
	if (!model->node_influences)
	{
		FREE (nodes);
		FreeModel (model);
		return 0;
	}
	for (size_t j = 0; j < nnodes; j++)
	{
		node_influence_t *inf = model->node_influences + j;
		inf->weights = MALLOC (sizeof (*inf->weights));
		if (!inf->weights)
		{
			FREE (nodes);
			FreeModel (model);
			return 0;
		}
		inf->num_weights = 1;
		inf->weights[0].bone_idx = (int)j;
		inf->weights[0].weight = 1.0f;
		// inverse bind = inverse of the baked world
		const float *m = nodes[j].world;
		const double det = (double)m[0] * (m[5] * m[10] - m[6] * m[9])
			- (double)m[1] * (m[4] * m[10] - m[6] * m[8])
			+ (double)m[2] * (m[4] * m[9] - m[5] * m[8]);
		float *ib = model->joints[j].inverse_bind;
		if (fabs (det) > 1e-20)
		{
			const float d = (float)(1.0 / det);
			ib[0] = (m[5] * m[10] - m[6] * m[9]) * d;
			ib[1] = (m[2] * m[9] - m[1] * m[10]) * d;
			ib[2] = (m[1] * m[6] - m[2] * m[5]) * d;
			ib[4] = (m[6] * m[8] - m[4] * m[10]) * d;
			ib[5] = (m[0] * m[10] - m[2] * m[8]) * d;
			ib[6] = (m[2] * m[4] - m[0] * m[6]) * d;
			ib[8] = (m[4] * m[9] - m[5] * m[8]) * d;
			ib[9] = (m[1] * m[8] - m[0] * m[9]) * d;
			ib[10] = (m[0] * m[5] - m[1] * m[4]) * d;
			ib[3] = -(ib[0] * m[3] + ib[1] * m[7] + ib[2] * m[11]);
			ib[7] = -(ib[4] * m[3] + ib[5] * m[7] + ib[6] * m[11]);
			ib[11] = -(ib[8] * m[3] + ib[9] * m[7] + ib[10] * m[11]);
			model->joints[j].has_inverse_bind = 1;
		}
	}

	//--- materials/textures discovered through draw elements ---
	// First pass: collect (node, draw) list to size meshes.
	typedef struct
	{
		size_t node_j;
		uint mat, batch;
	} lmb_part_t;
	lmb_part_t *parts = 0;
	size_t nparts = 0, cap_parts = 0;
	bool *batch_seen = CALLOC (LMB_MAX_SECT, sizeof (*batch_seen));
	bool *mat_seen = CALLOC (LMB_MAX_SECT, sizeof (*mat_seen));
	if (!batch_seen || !mat_seen)
	{
		FREE (batch_seen);
		FREE (mat_seen);
		FREE (nodes);
		FreeModel (model);
		return 0;
	}
	for (size_t j = 0; j < nnodes; j++)
	{
		const u8 *np = data + (u64)o.graph + (u64)nodes[j].node * 140;
		const uint dcnt = lmb_be16 (np + 76);
		const uint doff = lmb_be32 (np + 80);
		if (dcnt > 4096 || (u64)o.graph + doff + (u64)dcnt * 4 > size)
		{
			FREE (batch_seen);
			FREE (mat_seen);
			FREE (parts);
			FREE (nodes);
			FreeModel (model);
			return 0;
		}
		for (uint d = 0; d < dcnt; d++)
		{
			const u8 *dp = data + o.graph + doff + d * 4;
			const int mi = lmb_be16s (dp), bi = lmb_be16s (dp + 2);
			if (mi < 0 || mi >= (int)LMB_MAX_SECT || bi < 0 || bi >= (int)LMB_MAX_SECT)
			{
				FREE (batch_seen);
				FREE (mat_seen);
				FREE (parts);
				FREE (nodes);
				FreeModel (model);
				return 0;
			}
			if (nparts >= cap_parts)
			{
				const size_t nc = cap_parts ? cap_parts * 2 : 64;
				lmb_part_t *nn = REALLOC (parts, nc * sizeof (*nn));
				if (!nn)
				{
					FREE (batch_seen);
					FREE (mat_seen);
					FREE (parts);
					FREE (nodes);
					FreeModel (model);
					return 0;
				}
				parts = nn;
				cap_parts = nc;
			}
			parts[nparts].node_j = j;
			parts[nparts].mat = (uint)mi;
			parts[nparts].batch = (uint)bi;
			nparts++;
			mat_seen[mi] = true;
			batch_seen[bi] = true;
		}
	}
	if (!nparts)
	{
		FREE (batch_seen);
		FREE (mat_seen);
		FREE (parts);
		FREE (nodes);
		FreeModel (model);
		return 0;
	}

	// texture headers reachable from used materials
	uint ntex = 0;
	for (uint i = 0; i < LMB_MAX_SECT; i++)
		if (mat_seen[i])
		{
			const u64 mo = (u64)o.mat + (u64)i * 40;
			if (mo + 40 > size)
			{
				FREE (batch_seen);
				FREE (mat_seen);
				FREE (parts);
				FREE (nodes);
				FreeModel (model);
				return 0;
			}
			for (int k = 0; k < 8; k++)
			{
				const int si = lmb_be16s (data + mo + 8 + k * 2);
				if (si < 0)
					continue;
				if (si >= (int)LMB_MAX_SECT || (u64)o.samp + (u64)si * 20 + 20 > size)
				{
					FREE (batch_seen);
					FREE (mat_seen);
					FREE (parts);
					FREE (nodes);
					FreeModel (model);
					return 0;
				}
				const int ti = lmb_be16s (data + o.samp + si * 20);
				if (ti < 0 || ti >= (int)LMB_MAX_SECT
					|| (u64)o.tex + (u64)ti * 12 + 12 > size)
				{
					FREE (batch_seen);
					FREE (mat_seen);
					FREE (parts);
					FREE (nodes);
					FreeModel (model);
					return 0;
				}
				if ((uint)ti + 1 > ntex)
					ntex = (uint)ti + 1;
			}
		}
	// validate texture pixel ranges
	for (uint i = 0; i < ntex; i++)
	{
		const u8 *tp = data + o.tex + i * 12;
		const uint w = lmb_be16 (tp), hh = lmb_be16 (tp + 2), gx = tp[4];
		const uint need = lmb_gx_size (gx, w, hh);
		const uint ioff = lmb_be32 (tp + 8);
		if (!w || !hh || w > 2048 || hh > 2048 || !need || (u64)o.tex + ioff + need > size)
		{
			FREE (batch_seen);
			FREE (mat_seen);
			FREE (parts);
			FREE (nodes);
			FreeModel (model);
			return 0;
		}
	}

	// materials
	uint nmat = 0;
	for (uint i = 0; i < LMB_MAX_SECT; i++)
		if (mat_seen[i] && i + 1 > nmat)
			nmat = i + 1;
	model->num_materials = nmat ? nmat : 1;
	model->materials = CALLOC (model->num_materials, sizeof (*model->materials));
	if (!model->materials)
	{
		FREE (batch_seen);
		FREE (mat_seen);
		FREE (parts);
		FREE (nodes);
		FreeModel (model);
		return 0;
	}
	for (size_t i = 0; i < model->num_materials; i++)
	{
		material_t *m = model->materials + i;
		snprintf (m->name, sizeof (m->name), "Material%u", (uint)i);
		m->diffuse[0] = m->diffuse[1] = m->diffuse[2] = m->diffuse[3] = 1.0f;
		if (!mat_seen[i])
			continue;
		const u8 *mp = data + o.mat + i * 40;
		m->diffuse[0] = mp[3] / 255.0f;
		m->diffuse[1] = mp[4] / 255.0f;
		m->diffuse[2] = mp[5] / 255.0f;
		m->diffuse[3] = mp[6] / 255.0f;
		for (int k = 0; k < 8 && m->num_textures < 8; k++)
		{
			const int si = lmb_be16s (mp + 8 + k * 2);
			if (si < 0)
				continue;
			const int ti = lmb_be16s (data + o.samp + si * 20);
			const int kk = m->num_textures++;
			snprintf (m->textures[kk], sizeof (m->textures[kk]), "Texture%d.png", ti);
			const u8 wu = data[o.samp + si * 20 + 4], wv = data[o.samp + si * 20 + 5];
			m->wrap_s[kk] = wu == 0 ? 0 : wu == 2 ? 2 : 1;
			m->wrap_t[kk] = wv == 0 ? 0 : wv == 2 ? 2 : 1;
			m->min_filter[kk] = m->mag_filter[kk] = 1;
		}
	}

	// meshes
	model->num_meshes = nparts;
	model->meshes = CALLOC (nparts ? nparts : 1, sizeof (*model->meshes));
	if (!model->meshes)
	{
		FREE (batch_seen);
		FREE (mat_seen);
		FREE (parts);
		FREE (nodes);
		FreeModel (model);
		return 0;
	}
	for (size_t pi = 0; pi < nparts; pi++)
	{
		mesh_t *mesh = model->meshes + pi;
		snprintf (mesh->name, sizeof (mesh->name), "Mesh%u", (uint)pi);
		mesh->material_idx = parts[pi].mat < model->num_materials ? (int)parts[pi].mat : 0;
		lmb_soup_t soup = { 0 };
		bool hn = false, hu = false;
		if (!lmb_walk_batch (data, (uint)size, &o, parts[pi].batch, &soup, &hn, &hu)
			|| !soup.num || soup.num % 3)
		{
			FREE (soup.v);
			FREE (batch_seen);
			FREE (mat_seen);
			FREE (parts);
			FREE (nodes);
			FreeModel (model);
			return 0;
		}
		for (size_t c = 0; c < soup.num; c++)
		{
			if (soup.v[c].pos < 0
				|| (hn && soup.v[c].nrm < 0)
				|| (hu && soup.v[c].uv < 0))
			{
				FREE (soup.v);
				FREE (batch_seen);
				FREE (mat_seen);
				FREE (parts);
				FREE (nodes);
				FreeModel (model);
				return 0;
			}
		}
		// pools keyed by index
		int *vpos = MALLOC (soup.num * sizeof (*vpos));
		int *vnrm = MALLOC (soup.num * sizeof (*vnrm));
		int *vuv = MALLOC (soup.num * sizeof (*vuv));
		int *posmap = 0, *nrmmap = 0, *uvmap = 0;
		{
			uint maxp = 0, maxn = 0, maxu = 0;
			for (size_t c = 0; c < soup.num; c++)
			{
				if ((uint)soup.v[c].pos + 1 > maxp)
					maxp = (uint)soup.v[c].pos + 1;
				if (hn && (uint)soup.v[c].nrm + 1 > maxn)
					maxn = (uint)soup.v[c].nrm + 1;
				if (hu && (uint)soup.v[c].uv + 1 > maxu)
					maxu = (uint)soup.v[c].uv + 1;
			}
			if (maxp > LMB_MAX_VERTS || maxn > LMB_MAX_VERTS || maxu > LMB_MAX_VERTS)
			{
				FREE (vpos);
				FREE (vnrm);
				FREE (vuv);
				FREE (soup.v);
				FREE (batch_seen);
				FREE (mat_seen);
				FREE (parts);
				FREE (nodes);
				FreeModel (model);
				return 0;
			}
			posmap = MALLOC ((maxp ? maxp : 1) * sizeof (*posmap));
			nrmmap = MALLOC ((maxn ? maxn : 1) * sizeof (*nrmmap));
			uvmap = MALLOC ((maxu ? maxu : 1) * sizeof (*uvmap));
			if (!vpos || !vnrm || !vuv || !posmap || !nrmmap || !uvmap)
			{
				FREE (vpos);
				FREE (vnrm);
				FREE (vuv);
				FREE (posmap);
				FREE (nrmmap);
				FREE (uvmap);
				FREE (soup.v);
				FREE (batch_seen);
				FREE (mat_seen);
				FREE (parts);
				FREE (nodes);
				FreeModel (model);
				return 0;
			}
			for (uint k = 0; k < maxp; k++)
				posmap[k] = -1;
			for (uint k = 0; k < maxn; k++)
				nrmmap[k] = -1;
			for (uint k = 0; k < maxu; k++)
				uvmap[k] = -1;
		}
		mesh->positions = MALLOC (soup.num * sizeof (*mesh->positions));
		mesh->position_node = MALLOC (soup.num * sizeof (*mesh->position_node));
		mesh->normals = hn ? MALLOC (soup.num * sizeof (*mesh->normals)) : 0;
		mesh->texcoords = hu ? MALLOC (soup.num * sizeof (*mesh->texcoords)) : 0;
		mesh->vertices = MALLOC (soup.num * sizeof (*mesh->vertices));
		if (!mesh->positions || !mesh->position_node || !mesh->vertices
			|| (hn && !mesh->normals) || (hu && !mesh->texcoords))
		{
			FREE (vpos);
			FREE (vnrm);
			FREE (vuv);
			FREE (posmap);
			FREE (nrmmap);
			FREE (uvmap);
			FREE (soup.v);
			FREE (batch_seen);
			FREE (mat_seen);
			FREE (parts);
			FREE (nodes);
			FreeModel (model);
			return 0;
		}
		size_t np2 = 0, nn2 = 0, nu2 = 0;
		const float *W = nodes[parts[pi].node_j].world;
		for (size_t c = 0; c < soup.num; c++)
		{
			const int pii = soup.v[c].pos;
			if (posmap[pii] < 0)
			{
				const u64 po = (u64)o.pos + (u64)pii * 6;
				if (po + 6 > size)
				{
					FREE (vpos);
					FREE (vnrm);
					FREE (vuv);
					FREE (posmap);
					FREE (nrmmap);
					FREE (uvmap);
					FREE (soup.v);
					FREE (batch_seen);
					FREE (mat_seen);
					FREE (parts);
					FREE (nodes);
					FreeModel (model);
					return 0;
				}
				float x = (float)lmb_be16s (data + po), y = (float)lmb_be16s (data + po + 2),
					  z = (float)lmb_be16s (data + po + 4);
				lmb_xpos (W, &x, &y, &z);
				mesh->positions[np2].x = x;
				mesh->positions[np2].y = y;
				mesh->positions[np2].z = z;
				mesh->position_node[np2] = (int)parts[pi].node_j;
				posmap[pii] = (int)np2++;
			}
			vpos[c] = posmap[pii];
			if (hn)
			{
				const int nii = soup.v[c].nrm;
				if (nrmmap[nii] < 0)
				{
					const u64 no2 = (u64)o.nrm + (u64)nii * 12;
					if (no2 + 12 > size)
					{
						FREE (vpos);
						FREE (vnrm);
						FREE (vuv);
						FREE (posmap);
						FREE (nrmmap);
						FREE (uvmap);
						FREE (soup.v);
						FREE (batch_seen);
						FREE (mat_seen);
						FREE (parts);
						FREE (nodes);
						FreeModel (model);
						return 0;
					}
					float x = lmb_bef32 (data + no2), y = lmb_bef32 (data + no2 + 4),
						  z = lmb_bef32 (data + no2 + 8);
					if (!isfinite (x) || !isfinite (y) || !isfinite (z))
					{
						FREE (vpos);
						FREE (vnrm);
						FREE (vuv);
						FREE (posmap);
						FREE (nrmmap);
						FREE (uvmap);
						FREE (soup.v);
						FREE (batch_seen);
						FREE (mat_seen);
						FREE (parts);
						FREE (nodes);
						FreeModel (model);
						return 0;
					}
					lmb_xnrm (W, &x, &y, &z);
					mesh->normals[nn2].x = x;
					mesh->normals[nn2].y = y;
					mesh->normals[nn2].z = z;
					nrmmap[nii] = (int)nn2++;
				}
				vnrm[c] = nrmmap[nii];
			}
			else
				vnrm[c] = -1;
			if (hu)
			{
				const int uii = soup.v[c].uv;
				if (uvmap[uii] < 0)
				{
					const u64 uo = (u64)o.uv + (u64)uii * 8;
					if (uo + 8 > size)
					{
						FREE (vpos);
						FREE (vnrm);
						FREE (vuv);
						FREE (posmap);
						FREE (nrmmap);
						FREE (uvmap);
						FREE (soup.v);
						FREE (batch_seen);
						FREE (mat_seen);
						FREE (parts);
						FREE (nodes);
						FreeModel (model);
						return 0;
					}
					float u = lmb_bef32 (data + uo), v = lmb_bef32 (data + uo + 4);
					if (!isfinite (u) || !isfinite (v))
					{
						FREE (vpos);
						FREE (vnrm);
						FREE (vuv);
						FREE (posmap);
						FREE (nrmmap);
						FREE (uvmap);
						FREE (soup.v);
						FREE (batch_seen);
						FREE (mat_seen);
						FREE (parts);
						FREE (nodes);
						FreeModel (model);
						return 0;
					}
					mesh->texcoords[nu2].u = u;
					mesh->texcoords[nu2].v = v;
					uvmap[uii] = (int)nu2++;
				}
				vuv[c] = uvmap[uii];
			}
			else
				vuv[c] = -1;
		}
		mesh->num_positions = np2;
		mesh->num_normals = nn2;
		if (!hn)
		{
			FREE (mesh->normals);
			mesh->normals = 0;
		}
		mesh->num_texcoords = nu2;
		if (!hu)
		{
			FREE (mesh->texcoords);
			mesh->texcoords = 0;
		}
		mesh->num_vertices = soup.num;
		for (size_t c = 0; c < soup.num; c++)
		{
			vertex_t *v = mesh->vertices + c;
			v->position_idx = vpos[c];
			v->normal_idx = vnrm[c];
			v->tangent_idx = -1;
			v->texcoord_idx = vuv[c];
			v->matrix_idx = -1;
			v->color_idx[0] = v->color_idx[1] = -1;
			for (int k = 0; k < 7; k++)
				v->extra_texcoord_idx[k] = -1;
		}
		FREE (vpos);
		FREE (vnrm);
		FREE (vuv);
		FREE (posmap);
		FREE (nrmmap);
		FREE (uvmap);
		FREE (soup.v);
	}
	FREE (batch_seen);
	FREE (mat_seen);
	FREE (parts);
	FREE (nodes);
	return model;
}

enumError DecodeLMBIN (const u8 *data, uint size, ccp out_path)
{
	lmb_off_t o;
	if (!lmb_read_hdr (data, size, &o))
		return ERR_NOTHING_TO_DO;
	model_t *model = ParseLMBIN (data, size);
	if (!model)
		return ERR_NOTHING_TO_DO;

	// sibling PNGs for reachable GX textures
	uint ntex = 0;
	for (size_t i = 0; i < model->num_materials; i++)
		for (int k = 0; k < model->materials[i].num_textures; k++)
		{
			uint ti = 0;
			if (sscanf (model->materials[i].textures[k], "Texture%u.png", &ti) == 1 && ti + 1 > ntex)
				ntex = ti + 1;
		}
	for (uint i = 0; i < ntex; i++)
	{
		const u8 *tp = data + o.tex + i * 12;
		const uint w = lmb_be16 (tp), hh = lmb_be16 (tp + 2), gx = tp[4];
		const uint need = lmb_gx_size (gx, w, hh);
		const uint ioff = lmb_be32 (tp + 8);
		u8 *rgba = 0;
		if (!DecodeGXTexture_RGBA (&rgba, w, hh, gx, data + o.tex + ioff, need, 0, 0, 0))
		{
			char path[PATH_MAX], name[64];
			snprintf (name, sizeof (name), "Texture%u.png", i);
			ccp slash = strrchr (out_path, '/');
			const uint dlen = slash ? (uint)(slash - out_path + 1) : 0;
			if (dlen + strlen (name) + 1 < sizeof (path))
			{
				memcpy (path, out_path, dlen);
				strcpy (path + dlen, name);
				SaveDecodedRGBAToPNG (rgba, w, hh, &be_func, path, 0, true);
			}
			else
				FREE (rgba);
		}
	}

	const int rc = ExportModelToGLB (model, out_path);
	FreeModel (model);
	return rc == 0 ? ERR_OK : ERR_CANT_CREATE;
}

// ---- encoder ----

enumError EncodeLMBIN (const model_t *model, u8 **out, uint *out_size)
{
	if (!out || !out_size || !model || !model->num_meshes)
		return ERR_INVALID_DATA;
	// A jointless (pure static) model still needs one scene-graph node;
	// synthesize an identity root via a shallow model copy.
	model_t tmp;
	joint_t root;
	const model_t *mdl = model;
	if (!mdl->num_joints)
	{
		memset (&tmp, 0, sizeof (tmp));
		memset (&root, 0, sizeof (root));
		tmp = *model;
		snprintf (root.name, sizeof (root.name), "Node0");
		root.parent_idx = -1;
		root.scale.x = root.scale.y = root.scale.z = 1.0f;
		tmp.joints = &root;
		tmp.num_joints = 1;
		mdl = &tmp;
	}
	const size_t nm = mdl->num_meshes, nj = mdl->num_joints;
	if (!nj || nm > LMB_MAX_SECT || nj > LMB_MAX_SECT)
		return ERR_INVALID_DATA;

	// world matrices per joint (composed TRS, parent chains)
	float (*worlds)[12] = CALLOC (nj, sizeof (*worlds));
	if (!worlds)
		return ERR_OUT_OF_MEMORY;
	for (size_t j = 0; j < nj; j++)
	{
		const joint_t *jt = mdl->joints + j;
		float s[3] = { jt->scale.x ? jt->scale.x : 1, jt->scale.y ? jt->scale.y : 1,
			jt->scale.z ? jt->scale.z : 1 };
		float r[3] = { jt->rotate.x, jt->rotate.y, jt->rotate.z };
		float t[3] = { jt->translate.x, jt->translate.y, jt->translate.z };
		float local[12];
		lmb_trs (local, s, r, t);
		if (jt->parent_idx >= 0 && (size_t)jt->parent_idx < nj)
			lmb_mul43 (worlds[j], worlds[jt->parent_idx], local);
		else
			memcpy (worlds[j], local, sizeof (worlds[j]));
	}
	// inverse worlds for un-baking
	float (*iworlds)[12] = CALLOC (nj, sizeof (*iworlds));
	if (!iworlds)
	{
		FREE (worlds);
		return ERR_OUT_OF_MEMORY;
	}
	for (size_t j = 0; j < nj; j++)
	{
		// adjugate inverse of 3x4
		const float *m = worlds[j];
		const double det = (double)m[0] * (m[5] * m[10] - m[6] * m[9])
			- (double)m[1] * (m[4] * m[10] - m[6] * m[8])
			+ (double)m[2] * (m[4] * m[9] - m[5] * m[8]);
		if (fabs (det) < 1e-20)
		{
			FREE (worlds);
			FREE (iworlds);
			return ERR_INVALID_DATA;
		}
		const float d = (float)(1.0 / det);
		float *o2 = iworlds[j];
		o2[0] = (m[5] * m[10] - m[6] * m[9]) * d;
		o2[1] = (m[2] * m[9] - m[1] * m[10]) * d;
		o2[2] = (m[1] * m[6] - m[2] * m[5]) * d;
		o2[4] = (m[6] * m[8] - m[4] * m[10]) * d;
		o2[5] = (m[0] * m[10] - m[2] * m[8]) * d;
		o2[6] = (m[2] * m[4] - m[0] * m[6]) * d;
		o2[8] = (m[4] * m[9] - m[5] * m[8]) * d;
		o2[9] = (m[1] * m[8] - m[0] * m[9]) * d;
		o2[10] = (m[0] * m[5] - m[1] * m[4]) * d;
		o2[3] = -(o2[0] * m[3] + o2[1] * m[7] + o2[2] * m[11]);
		o2[7] = -(o2[4] * m[3] + o2[5] * m[7] + o2[6] * m[11]);
		o2[11] = -(o2[8] * m[3] + o2[9] * m[7] + o2[10] * m[11]);
	}

	// pools (positions quantised to s16, like retail)
	typedef struct
	{
		int x, y, z;
	} i3_t;
	i3_t *pospool = 0;
	size_t npos = 0, cap_pos = 0;
	float (*nrmpool)[3] = 0;
	size_t nnrm = 0, cap_nrm = 0;
	float (*uvpool)[2] = 0;
	size_t nuv = 0, cap_uv = 0;

	// per-mesh batch blobs + draw assignment
	typedef struct
	{
		u8 *blob;
		uint len, mat, node;
		bool hn, hu;
	} lmb_batch_t;
	lmb_batch_t *batches = CALLOC (nm, sizeof (*batches));
	if (!batches)
	{
		FREE (worlds);
		FREE (iworlds);
		return ERR_OUT_OF_MEMORY;
	}
	const size_t nmat = mdl->num_materials ? mdl->num_materials : 1;
	const size_t nimg = mdl->num_images;

	for (size_t m = 0; m < nm; m++)
	{
		const mesh_t *mesh = mdl->meshes + m;
		if (!mesh->num_vertices || mesh->num_vertices % 3)
		{
			FREE (worlds);
			FREE (iworlds);
			FREE (pospool);
			FREE (nrmpool);
			FREE (uvpool);
			for (size_t k = 0; k < m; k++)
				FREE (batches[k].blob);
			FREE (batches);
			return ERR_INVALID_DATA;
		}
		// dominant node for this mesh
		size_t *votes = CALLOC (nj, sizeof (*votes));
		if (!votes)
		{
			FREE (worlds);
			FREE (iworlds);
			FREE (pospool);
			FREE (nrmpool);
			FREE (uvpool);
			for (size_t k = 0; k < m; k++)
				FREE (batches[k].blob);
			FREE (batches);
			return ERR_OUT_OF_MEMORY;
		}
		for (size_t c = 0; c < mesh->num_vertices; c++)
		{
			const int pi = mesh->vertices[c].position_idx;
			int node = (pi >= 0 && (size_t)pi < mesh->num_positions) ? mesh->position_node[pi]
																	 : 0;
			if (node < 0 || (size_t)node >= nj)
				node = 0;
			votes[node]++;
		}
		size_t dom = 0;
		for (size_t k = 1; k < nj; k++)
			if (votes[k] > votes[dom])
				dom = k;
		FREE (votes);
		batches[m].node = (uint)dom;
		batches[m].mat = mesh->material_idx >= 0 && (size_t)mesh->material_idx < nmat
			? (uint)mesh->material_idx
			: 0;
		const bool hn = mesh->normals && mesh->num_normals > 0;
		const bool hu = mesh->texcoords && mesh->num_texcoords > 0;
		batches[m].hn = hn;
		batches[m].hu = hu;
		u8 *blob = MALLOC (3 + mesh->num_vertices * 8);
		if (!blob)
		{
			FREE (worlds);
			FREE (iworlds);
			FREE (pospool);
			FREE (nrmpool);
			FREE (uvpool);
			for (size_t k = 0; k < m; k++)
				FREE (batches[k].blob);
			FREE (batches);
			return ERR_OUT_OF_MEMORY;
		}
		u8 *bp = blob;
		*bp++ = 0x90;
		lmb_wr16 (bp, (u16)mesh->num_vertices);
		bp += 2;
		for (size_t c = 0; c < mesh->num_vertices; c++)
		{
			const vertex_t *v = mesh->vertices + c;
			const int pi = v->position_idx;
			float x = mesh->positions[pi].x, y = mesh->positions[pi].y, z = mesh->positions[pi].z;
			lmb_xpos (iworlds[dom], &x, &y, &z);
			if (x > 32767)
				x = 32767;
			if (x < -32768)
				x = -32768;
			if (y > 32767)
				y = 32767;
			if (y < -32768)
				y = -32768;
			if (z > 32767)
				z = 32767;
			if (z < -32768)
				z = -32768;
			i3_t P = { (int)lroundf (x), (int)lroundf (y), (int)lroundf (z) };
			size_t f = npos;
			for (size_t k = 0; k < npos; k++)
				if (pospool[k].x == P.x && pospool[k].y == P.y && pospool[k].z == P.z)
				{
					f = k;
					break;
				}
			if (f == npos)
			{
				if (npos >= cap_pos)
				{
					const size_t nc = cap_pos ? cap_pos * 2 : 1024;
					i3_t *nn = REALLOC (pospool, nc * sizeof (*nn));
					if (!nn)
					{
						FREE (blob);
						FREE (worlds);
						FREE (iworlds);
						FREE (pospool);
						FREE (nrmpool);
						FREE (uvpool);
						for (size_t k = 0; k < m; k++)
							FREE (batches[k].blob);
						FREE (batches);
						return ERR_OUT_OF_MEMORY;
					}
					pospool = nn;
					cap_pos = nc;
				}
				pospool[npos++] = P;
			}
			lmb_wr16 (bp, (u16)f);
			bp += 2;
			if (hn && v->normal_idx >= 0)
			{
				float nx = mesh->normals[v->normal_idx].x, ny = mesh->normals[v->normal_idx].y,
					  nz = mesh->normals[v->normal_idx].z;
				lmb_xnrm (iworlds[dom], &nx, &ny, &nz);
				size_t fn = nnrm;
				for (size_t k = 0; k < nnrm; k++)
					if (nrmpool[k][0] == nx && nrmpool[k][1] == ny && nrmpool[k][2] == nz)
					{
						fn = k;
						break;
					}
				if (fn == nnrm)
				{
					if (nnrm >= cap_nrm)
					{
						const size_t nc = cap_nrm ? cap_nrm * 2 : 1024;
						float(*nn)[3] = REALLOC (nrmpool, nc * sizeof (*nn));
						if (!nn)
						{
							FREE (blob);
							FREE (worlds);
							FREE (iworlds);
							FREE (pospool);
							FREE (nrmpool);
							FREE (uvpool);
							for (size_t k = 0; k < m; k++)
								FREE (batches[k].blob);
							FREE (batches);
							return ERR_OUT_OF_MEMORY;
						}
						nrmpool = nn;
						cap_nrm = nc;
					}
					nrmpool[nnrm][0] = nx;
					nrmpool[nnrm][1] = ny;
					nrmpool[nnrm][2] = nz;
					nnrm++;
				}
				lmb_wr16 (bp, (u16)fn);
				bp += 2;
			}
			if (hu && v->texcoord_idx >= 0)
			{
				const float uu = mesh->texcoords[v->texcoord_idx].u,
							vv = mesh->texcoords[v->texcoord_idx].v;
				size_t ft = nuv;
				for (size_t k = 0; k < nuv; k++)
					if (uvpool[k][0] == uu && uvpool[k][1] == vv)
					{
						ft = k;
						break;
					}
				if (ft == nuv)
				{
					if (nuv >= cap_uv)
					{
						const size_t nc = cap_uv ? cap_uv * 2 : 1024;
						float(*nn)[2] = REALLOC (uvpool, nc * sizeof (*nn));
						if (!nn)
						{
							FREE (blob);
							FREE (worlds);
							FREE (iworlds);
							FREE (pospool);
							FREE (nrmpool);
							FREE (uvpool);
							for (size_t k = 0; k < m; k++)
								FREE (batches[k].blob);
							FREE (batches);
							return ERR_OUT_OF_MEMORY;
						}
						uvpool = nn;
						cap_uv = nc;
					}
					uvpool[nuv][0] = uu;
					uvpool[nuv][1] = vv;
					nuv++;
				}
				lmb_wr16 (bp, (u16)ft);
				bp += 2;
			}
		}
		batches[m].blob = blob;
		batches[m].len = (uint)(bp - blob);
	}
	if (npos > 65535 || nnrm > 65535 || nuv > 65535)
	{
		FREE (worlds);
		FREE (iworlds);
		FREE (pospool);
		FREE (nrmpool);
		FREE (uvpool);
		for (size_t k = 0; k < nm; k++)
			FREE (batches[k].blob);
		FREE (batches);
		return ERR_INVALID_DATA;
	}

	// layout
	const uint tex_off = 64;
	uint cur = tex_off + (uint)nimg * 12;
	// texture pixel data (zero-filled CMPR of PNG-IHDR dims)
	uint *tex_w = 0, *tex_h = 0, *tex_sz = 0;
	if (nimg)
	{
		tex_w = MALLOC (nimg * sizeof (*tex_w));
		tex_h = MALLOC (nimg * sizeof (*tex_h));
		tex_sz = MALLOC (nimg * sizeof (*tex_sz));
		if (!tex_w || !tex_h || !tex_sz)
		{
			FREE (tex_w);
			FREE (tex_h);
			FREE (tex_sz);
			FREE (worlds);
			FREE (iworlds);
			FREE (pospool);
			FREE (nrmpool);
			FREE (uvpool);
			for (size_t k = 0; k < nm; k++)
				FREE (batches[k].blob);
			FREE (batches);
			return ERR_OUT_OF_MEMORY;
		}
		for (size_t i = 0; i < nimg; i++)
		{
			uint w = 8, hh = 8;
			const model_image_t *im = mdl->images + i;
			if (im->size >= 24 && !memcmp (im->data, "\x89PNG\r\n\x1a\n", 8)
				&& !memcmp (im->data + 12, "IHDR", 4))
			{
				w = (uint)im->data[16] << 24 | (uint)im->data[17] << 16 | (uint)im->data[18] << 8
					| im->data[19];
				hh = (uint)im->data[20] << 24 | (uint)im->data[21] << 16 | (uint)im->data[22] << 8
					| im->data[23];
				if (!w || !hh || w > 2048 || hh > 2048)
				{
					w = 8;
					hh = 8;
				}
			}
			tex_w[i] = w;
			tex_h[i] = hh;
			tex_sz[i] = lmb_gx_size (14, w, hh);
		}
	}
	const uint pix_off = cur;
	for (size_t i = 0; i < nimg; i++)
		cur += tex_sz[i];
	cur = (cur + 31) & ~31u; // Align(32) like the reference
	const uint samp_off = cur;
	uint nsamp = 0;
	for (size_t i = 0; i < nmat; i++)
	{
		const material_t *mt = mdl->num_materials > i ? mdl->materials + i : 0;
		uint nl = mt && mt->num_textures > 0 ? (uint)mt->num_textures : 1;
		if (nl > 8)
			nl = 8;
		nsamp += nl;
	}
	cur += nsamp * 20;
	const uint pos_off = cur;
	cur += (uint)npos * 6;
	cur = (cur + 31) & ~31u;
	const uint nrm_off = cur;
	cur += (uint)nnrm * 12;
	cur = (cur + 31) & ~31u;
	const uint uv_off = cur;
	cur += (uint)nuv * 8;
	cur = (cur + 31) & ~31u;
	// attribute pools unused by this writer; keep header order
	// (at1, at2 precede uv, at3-at5 follow it like the reference).
	const uint at1 = uv_off, at2 = uv_off;
	const uint at3 = cur, at4 = cur, at5 = cur;
	const uint mat_off = cur;
	cur += (uint)nmat * 40;
	const uint batch_off = cur;
	cur += (uint)nm * 24;
	// batch display lists
	uint *batch_dl = MALLOC (nm * sizeof (*batch_dl));
	if (!batch_dl)
	{
		FREE (tex_w);
		FREE (tex_h);
		FREE (tex_sz);
		FREE (worlds);
		FREE (iworlds);
		FREE (pospool);
		FREE (nrmpool);
		FREE (uvpool);
		for (size_t k = 0; k < nm; k++)
			FREE (batches[k].blob);
		FREE (batches);
		return ERR_OUT_OF_MEMORY;
	}
	for (size_t m = 0; m < nm; m++)
	{
		batch_dl[m] = cur;
		const uint padded = (batches[m].len + 31) & ~31u;
		cur += padded;
	}
	const uint graph_off = cur;
	cur += (uint)nj * 140;
	// draw-element lists after the node array
	uint *draw_off = MALLOC (nj * sizeof (*draw_off));
	uint *draw_cnt = CALLOC (nj, sizeof (*draw_cnt));
	if (!draw_off || !draw_cnt)
	{
		FREE (draw_off);
		FREE (draw_cnt);
		FREE (batch_dl);
		FREE (tex_w);
		FREE (tex_h);
		FREE (tex_sz);
		FREE (worlds);
		FREE (iworlds);
		FREE (pospool);
		FREE (nrmpool);
		FREE (uvpool);
		for (size_t k = 0; k < nm; k++)
			FREE (batches[k].blob);
		FREE (batches);
		return ERR_OUT_OF_MEMORY;
	}
	for (size_t m = 0; m < nm; m++)
		draw_cnt[batches[m].node]++;
	for (size_t j = 0; j < nj; j++)
	{
		draw_off[j] = cur;
		cur += (uint)draw_cnt[j] * 4;
	}

	u8 *buf = CALLOC (1, cur ? cur : 1);
	if (!buf)
	{
		FREE (draw_off);
		FREE (draw_cnt);
		FREE (batch_dl);
		FREE (tex_w);
		FREE (tex_h);
		FREE (tex_sz);
		FREE (worlds);
		FREE (iworlds);
		FREE (pospool);
		FREE (nrmpool);
		FREE (uvpool);
		for (size_t k = 0; k < nm; k++)
			FREE (batches[k].blob);
		FREE (batches);
		return ERR_OUT_OF_MEMORY;
	}
	buf[0] = 2;
	snprintf ((char *)buf + 1, 11, "nintoolbox");
	lmb_wr32 (buf + 12, tex_off);
	lmb_wr32 (buf + 16, samp_off);
	lmb_wr32 (buf + 20, pos_off);
	lmb_wr32 (buf + 24, nrm_off);
	lmb_wr32 (buf + 28, at1);
	lmb_wr32 (buf + 32, at2);
	lmb_wr32 (buf + 36, uv_off);
	lmb_wr32 (buf + 40, at3);
	lmb_wr32 (buf + 44, at4);
	lmb_wr32 (buf + 48, at5);
	lmb_wr32 (buf + 52, mat_off);
	lmb_wr32 (buf + 56, batch_off);
	lmb_wr32 (buf + 60, graph_off);

	uint po = pix_off;
	for (size_t i = 0; i < nimg; i++)
	{
		u8 *tp = buf + tex_off + i * 12;
		lmb_wr16 (tp, (u16)tex_w[i]);
		lmb_wr16 (tp + 2, (u16)tex_h[i]);
		tp[4] = 14; // CMPR
		lmb_wr32 (tp + 8, po - tex_off);
		po += tex_sz[i];
	}

	uint si = 0;
	for (size_t i = 0; i < nmat; i++)
	{
		u8 *mp = buf + mat_off + i * 40;
		const material_t *mt = mdl->num_materials > i ? mdl->materials + i : 0;
		if (mt)
		{
			mp[3] = (u8)(mt->diffuse[0] * 255.0f);
			mp[4] = (u8)(mt->diffuse[1] * 255.0f);
			mp[5] = (u8)(mt->diffuse[2] * 255.0f);
			mp[6] = (u8)(mt->diffuse[3] * 255.0f);
		}
		else
			mp[3] = mp[4] = mp[5] = mp[6] = 255;
		uint nl = mt && mt->num_textures > 0 ? (uint)mt->num_textures : 1;
		if (nl > 8)
			nl = 8;
		for (uint t = 0; t < 8; t++)
			lmb_wr16 (mp + 8 + t * 2, 0xffff);
		for (uint t = 0; t < nl; t++)
		{
			int img = -1;
			if (mt && t < (uint)mt->num_textures && mt->textures[t][0])
			{
				for (size_t g = 0; g < nimg; g++)
				{
					char base[64];
					snprintf (base, sizeof (base), "Texture%u", (uint)g);
					if (!strcmp (mt->textures[t], mdl->images[g].name)
						|| strstr (mt->textures[t], base))
					{
						img = (int)g;
						break;
					}
				}
				if (img < 0 && nimg)
					img = 0;
			}
			lmb_wr16 (mp + 8 + t * 2, (u16)(si + t));
			u8 *sp = buf + samp_off + (si + t) * 20;
			lmb_wr16 (sp, img >= 0 ? (u16)img : (u16)0xffff);
			lmb_wr16 (sp + 2, (u16)0xffff);
			sp[4] = 1;
			sp[5] = 1;
		}
		si += nl;
	}

	for (size_t i = 0; i < npos; i++)
	{
		u8 *pp = buf + pos_off + i * 6;
		lmb_wr16 (pp, (u16)(pospool[i].x & 0xffff));
		lmb_wr16 (pp + 2, (u16)(pospool[i].y & 0xffff));
		lmb_wr16 (pp + 4, (u16)(pospool[i].z & 0xffff));
	}
	for (size_t i = 0; i < nnrm; i++)
	{
		lmb_wrf32 (buf + nrm_off + i * 12, nrmpool[i][0]);
		lmb_wrf32 (buf + nrm_off + i * 12 + 4, nrmpool[i][1]);
		lmb_wrf32 (buf + nrm_off + i * 12 + 8, nrmpool[i][2]);
	}
	for (size_t i = 0; i < nuv; i++)
	{
		lmb_wrf32 (buf + uv_off + i * 8, uvpool[i][0]);
		lmb_wrf32 (buf + uv_off + i * 8 + 4, uvpool[i][1]);
	}

	for (size_t m = 0; m < nm; m++)
	{
		u8 *bhp = buf + batch_off + m * 24;
		lmb_wr16 (bhp, (u16)(mdl->meshes[m].num_vertices / 3));
		lmb_wr16 (bhp + 2, (u16)((batches[m].len + 31) / 32));
		u32 attr = 0;
		if (batches[m].hn)
			attr |= LMB_GX_NRM;
		if (batches[m].hu)
			attr |= LMB_GX_TX (0);
		lmb_wr32 (bhp + 4, attr);
		bhp[8] = batches[m].hn ? 1 : 0;
		bhp[9] = 1;
		bhp[10] = batches[m].hu ? 1 : 0;
		bhp[11] = 0;
		lmb_wr32 (bhp + 12, batch_dl[m] - batch_off);
		memcpy (buf + batch_dl[m], batches[m].blob, batches[m].len);
	}

	// nodes + draw lists
	uint *dcursor = MALLOC (nj * sizeof (*dcursor));
	if (!dcursor)
	{
		FREE (draw_off);
		FREE (draw_cnt);
		FREE (batch_dl);
		FREE (tex_w);
		FREE (tex_h);
		FREE (tex_sz);
		FREE (worlds);
		FREE (iworlds);
		FREE (pospool);
		FREE (nrmpool);
		FREE (uvpool);
		for (size_t k = 0; k < nm; k++)
			FREE (batches[k].blob);
		FREE (batches);
		FREE (buf);
		return ERR_OUT_OF_MEMORY;
	}
	for (size_t j = 0; j < nj; j++)
		dcursor[j] = draw_off[j];
	// draw lists reference batch indices in mesh order
	for (size_t m = 0; m < nm; m++)
	{
		const uint node = batches[m].node;
		u8 *dp = buf + dcursor[node];
		lmb_wr16 (dp, (u16)batches[m].mat);
		lmb_wr16 (dp + 2, (u16)m);
		dcursor[node] += 4;
	}
	for (size_t j = 0; j < nj; j++)
	{
		u8 *np = buf + graph_off + j * 140;
		const joint_t *jt = mdl->joints + j;
		lmb_wr16 (np, jt->parent_idx >= 0 ? (u16)jt->parent_idx : 0xffff);
		int fc = -1, ns = -1;
		for (size_t k = 0; k < nj; k++)
			if (mdl->joints[k].parent_idx == (int)j)
			{
				fc = (int)k;
				break;
			}
		const int p = jt->parent_idx;
		for (size_t k = j + 1; k < nj; k++)
			if (mdl->joints[k].parent_idx == p)
			{
				ns = (int)k;
				break;
			}
		lmb_wr16 (np + 2, fc >= 0 ? (u16)fc : 0xffff);
		lmb_wr16 (np + 4, ns >= 0 ? (u16)ns : 0xffff);
		lmb_wr16 (np + 6, 0xffff);
		np[8] = 0;
		np[9] = 0;
		lmb_wr16 (np + 10, 0);
		lmb_wrf32 (np + 12, jt->scale.x ? jt->scale.x : 1);
		lmb_wrf32 (np + 16, jt->scale.y ? jt->scale.y : 1);
		lmb_wrf32 (np + 20, jt->scale.z ? jt->scale.z : 1);
		lmb_wrf32 (np + 24, jt->rotate.x);
		lmb_wrf32 (np + 28, jt->rotate.y);
		lmb_wrf32 (np + 32, jt->rotate.z);
		lmb_wrf32 (np + 36, jt->translate.x);
		lmb_wrf32 (np + 40, jt->translate.y);
		lmb_wrf32 (np + 44, jt->translate.z);
		// bbox: zero (unknown extents)
		lmb_wr16 (np + 76, (u16)draw_cnt[j]);
		lmb_wr16 (np + 78, 0);
		lmb_wr32 (np + 80, draw_off[j] - graph_off);
	}
	FREE (dcursor);

	FREE (draw_off);
	FREE (draw_cnt);
	FREE (batch_dl);
	FREE (tex_w);
	FREE (tex_h);
	FREE (tex_sz);
	FREE (worlds);
	FREE (iworlds);
	FREE (pospool);
	FREE (nrmpool);
	FREE (uvpool);
	for (size_t k = 0; k < nm; k++)
		FREE (batches[k].blob);
	FREE (batches);

	*out = buf;
	*out_size = cur;
	return ERR_OK;
}

enumError EncodeModelToLMBIN (const model_t *model, ccp out_path)
{
	u8 *buf = 0;
	uint size = 0;
	enumError err = EncodeLMBIN (model, &buf, &size);
	if (err || !buf)
	{
		FREE (buf);
		return err ? err : ERR_INVALID_DATA;
	}
	File_t F;
	err = CreateFileOpt (&F, true, out_path, false, out_path);
	if (!err && F.f && fwrite (buf, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing LM BIN failed: %s\n", out_path);
	ResetFile (&F, opt_preserve);
	FREE (buf);
	return err;
}
