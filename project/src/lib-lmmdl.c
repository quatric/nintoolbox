// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-lmmdl.h"
#include "lib-model-glb.h"
#include "lib-excite.h"
#include "lib-image.h"
#include <string.h>
#include <math.h>

//-----------------------------------------------------------------------------
///////////////		Luigi's Mansion actor model (.mdl)		///////////////
//-----------------------------------------------------------------------------
//
// See lib-lmmdl.h for provenance. On-disk summary (all big-endian):
//
//   header (128 bytes): u32 magic 0x04B40000, then counts (u16) and absolute
//     file offsets (u32) for nodes, shape packets (*2: main + LOD shadow
//     packets), matrices, weights, joint indices, weight counts, positions,
//     normals, colours, texcoords, textures (u32 offset array), materials,
//     samplers ("texture objects"), shapes and draw elements.
//   textures: u32 offset array -> each 32-byte header {u8 format, u8 pad,
//     u16 w, u16 h, 26 reserved} + GX tiled pixels. Format byte:
//     3=I4 4=I8 5=IA4 6=IA8 7=RGB565 8=RGB5A3 9=RGBA32 0x0A=CMPR.
//   material: 32-byte head {RGBA u8x4, u16 unk, u8 alpha, u8 num_tev,
//     u8 unk, 23 pad} + 8 tev stages of {u16 unk, u16 sampler, 7x f32}.
//   sampler: {u16 tex, u16 unk, u8 wrapU, wrapV, min, mag}.
//   shape: {u8 normalFlags, u8 x3, u16 packetCount, u16 packetBegin}.
//   draw element: {u16 material, u16 shape}.
//   node: {u16 index, child(rel), sibling(rel), unk, u16 shapeCount,
//     u16 shapeIndex, u32 pad} -- child/sibling are *relative* indices.
//   packet: {u32 dataOff, u32 dataSize, u16 unk, u16 matCount,
//     u16 matIdx[10]} + GX display list: u8 opcode (0x90 tris / 0x98 strips
//     / 0xA0 fans, 0 = pad), u16 count, then vertices. Normal mesh vertex:
//     s8 matrix, s8 tex0, s8 tex1, s16 pos, [s16 normal], [s16 tan, s16 bin
//     if shape.normalFlags > 1], [s16 colour], [s16 uv]. LOD mesh vertex:
//     s8 matrix, s16 pos, u8 normal.
//   matrices: JointCount x 12x f32 (3x4 row-major *inverse* bind).
//   weights: u8 counts, then f32 weights, then u16 joint indices.
//
// A matrix-slot id < JointCount binds rigidly to that joint; higher ids
// index the weight table (id - JointCount) for smooth skinning. Rigid
// vertices are stored joint-local and baked to world space on import,
// exactly like the reference converter; smooth vertices are already in
// model space and keep weight-table influences.

#define LMMDL_MAGIC 0x04b40000u
#define LMMDL_HDR_SIZE 128
#define LMMDL_MAX_NODES 4096
#define LMMDL_MAX_MESHES 4096
#define LMMDL_MAX_VERTS (8u << 20)

static inline u16 lmm_be16 (const u8 *p)
{
	return (u16)((u16)p[0] << 8 | p[1]);
}

static inline u32 lmm_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline float lmm_bef32 (const u8 *p)
{
	u32 u = lmm_be32 (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

static inline s16 lmm_be16s (const u8 *p)
{
	return (s16)lmm_be16 (p);
}

static inline void lmm_wr16 (u8 *p, u16 v)
{
	p[0] = (u8)(v >> 8);
	p[1] = (u8)v;
}

static inline void lmm_wr32 (u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

static inline void lmm_wrf32 (u8 *p, float f)
{
	u32 u;
	memcpy (&u, &f, 4);
	lmm_wr32 (p, u);
}

bool IsLMMDL (const u8 *data, size_t size)
{
	return data && size >= LMMDL_HDR_SIZE && lmm_be32 (data) == LMMDL_MAGIC;
}

// GX tiled size for the texture formats LM uses.
static uint lmm_gx_size (uint gx, uint w, uint h)
{
	uint bw = 4, bh = 4, bpp = 16;
	switch (gx)
	{
		case 0:
			bw = 8;
			bh = 8;
			bpp = 4;
			break; // I4
		case 1:
		case 2:
			bw = 8;
			bh = 4;
			bpp = 8;
			break; // I8 IA4
		case 3:
		case 4:
		case 5:
			bw = 4;
			bh = 4;
			bpp = 16;
			break; // IA8 RGB565 RGB5A3
		case 6:
			bw = 4;
			bh = 4;
			bpp = 32;
			break; // RGBA32
		case 14:
			bw = 8;
			bh = 8;
			bpp = 4;
			break; // CMPR
		default:
			return 0;
	}
	return ((w + bw - 1) / bw) * ((h + bh - 1) / bh) * (bw * bh * bpp / 8);
}

// LM texture format byte -> GX format id for DecodeGXTexture_RGBA().
static int lmm_tex_gx (u8 fmt)
{
	switch (fmt)
	{
		case 0x03:
			return 0; // I4
		case 0x04:
			return 1; // I8
		case 0x05:
			return 2; // IA4
		case 0x06:
			return 3; // IA8
		case 0x07:
			return 4; // RGB565
		case 0x08:
			return 5; // RGB5A3
		case 0x09:
			return 6; // RGBA32
		case 0x0a:
			return 14; // CMPR
		default:
			return -1;
	}
}

typedef struct
{
	uint faces, nodes, packets, weights, joints, verts, normals, colours, uvs;
	uint textures, samplers, elements, materials, shapes;
	u32 node_off, packet_off, matrix_off, weight_off, jidx_off, wcnt_off;
	u32 vert_off, normal_off, colour_off, uv_off;
	u32 tex_off, mat_off, samp_off, shape_off, elem_off;
} lmm_hdr_t;

static bool lmm_read_hdr (const u8 *data, uint size, lmm_hdr_t *h)
{
	if (!IsLMMDL (data, size))
		return false;
	memset (h, 0, sizeof (*h));
	h->faces = lmm_be16 (data + 4);
	h->nodes = lmm_be16 (data + 8);
	h->packets = lmm_be16 (data + 10);
	h->weights = lmm_be16 (data + 12);
	h->joints = lmm_be16 (data + 14);
	h->verts = lmm_be16 (data + 16);
	h->normals = lmm_be16 (data + 18);
	h->colours = lmm_be16 (data + 20);
	h->uvs = lmm_be16 (data + 22);
	h->textures = lmm_be16 (data + 32);
	h->samplers = lmm_be16 (data + 36);
	h->elements = lmm_be16 (data + 38);
	h->materials = lmm_be16 (data + 40);
	h->shapes = lmm_be16 (data + 42);
	h->node_off = lmm_be32 (data + 48);
	h->packet_off = lmm_be32 (data + 52);
	h->matrix_off = lmm_be32 (data + 56);
	h->weight_off = lmm_be32 (data + 60);
	h->jidx_off = lmm_be32 (data + 64);
	h->wcnt_off = lmm_be32 (data + 68);
	h->vert_off = lmm_be32 (data + 72);
	h->normal_off = lmm_be32 (data + 76);
	h->colour_off = lmm_be32 (data + 80);
	h->uv_off = lmm_be32 (data + 84);
	h->tex_off = lmm_be32 (data + 96);
	h->mat_off = lmm_be32 (data + 104);
	h->samp_off = lmm_be32 (data + 108);
	h->shape_off = lmm_be32 (data + 112);
	h->elem_off = lmm_be32 (data + 116);
	if (!h->joints || h->joints > LMMDL_MAX_NODES || h->nodes < h->joints
		|| h->nodes > LMMDL_MAX_NODES || h->elements > LMMDL_MAX_MESHES
		|| h->packets > LMMDL_MAX_MESHES || h->materials > LMMDL_MAX_MESHES
		|| h->shapes > LMMDL_MAX_MESHES || h->textures > 1024 || h->samplers > 1024
		|| h->weights > 8192 || h->verts > LMMDL_MAX_VERTS
		|| h->normals > LMMDL_MAX_VERTS || h->uvs > LMMDL_MAX_VERTS
		|| h->colours > LMMDL_MAX_VERTS)
		return false;
	return true;
}

// 3x4 row-major affine helpers.
static bool lmm_invert43 (float out[12], const float m[12])
{
	const double det = (double)m[0] * (m[5] * m[10] - m[6] * m[9])
		- (double)m[1] * (m[4] * m[10] - m[6] * m[8])
		+ (double)m[2] * (m[4] * m[9] - m[5] * m[8]);
	if (fabs (det) < 1e-20)
		return false;
	const float d = (float)(1.0 / det);
	out[0] = (m[5] * m[10] - m[6] * m[9]) * d;
	out[1] = (m[2] * m[9] - m[1] * m[10]) * d;
	out[2] = (m[1] * m[6] - m[2] * m[5]) * d;
	out[4] = (m[6] * m[8] - m[4] * m[10]) * d;
	out[5] = (m[0] * m[10] - m[2] * m[8]) * d;
	out[6] = (m[2] * m[4] - m[0] * m[6]) * d;
	out[8] = (m[4] * m[9] - m[5] * m[8]) * d;
	out[9] = (m[1] * m[8] - m[0] * m[9]) * d;
	out[10] = (m[0] * m[5] - m[1] * m[4]) * d;
	out[3] = -(out[0] * m[3] + out[1] * m[7] + out[2] * m[11]);
	out[7] = -(out[4] * m[3] + out[5] * m[7] + out[6] * m[11]);
	out[11] = -(out[8] * m[3] + out[9] * m[7] + out[10] * m[11]);
	return true;
}

static void lmm_mul43 (float out[12], const float a[12], const float b[12])
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

static void lmm_xform_pos (const float m[12], float *x, float *y, float *z)
{
	const float px = *x, py = *y, pz = *z;
	*x = m[0] * px + m[1] * py + m[2] * pz + m[3];
	*y = m[4] * px + m[5] * py + m[6] * pz + m[7];
	*z = m[8] * px + m[9] * py + m[10] * pz + m[11];
}

static void lmm_xform_nrm (const float m[12], float *x, float *y, float *z)
{
	const float px = *x, py = *y, pz = *z;
	*x = m[0] * px + m[1] * py + m[2] * pz;
	*y = m[4] * px + m[5] * py + m[6] * pz;
	*z = m[8] * px + m[9] * py + m[10] * pz;
}

// Decompose a 3x4 affine (assumed rotation*scale, no shear) into translate,
// ZYX euler degrees (the convention dae_joint_trs() rebuilds) and scale.
static void lmm_decompose (const float m[12], vec3_t *t, vec3_t *r, vec3_t *s)
{
	t->x = m[3];
	t->y = m[7];
	t->z = m[11];
	float sx = sqrtf (m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
	float sy = sqrtf (m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
	float sz = sqrtf (m[2] * m[2] + m[6] * m[6] + m[10] * m[10]);
	if (!(sx > 1e-12f))
		sx = 1.0f;
	if (!(sy > 1e-12f))
		sy = 1.0f;
	if (!(sz > 1e-12f))
		sz = 1.0f;
	s->x = sx;
	s->y = sy;
	s->z = sz;
	const float r00 = m[0] / sx, r10 = m[4] / sx, r20 = m[8] / sx;
	const float r01 = m[1] / sy, r11 = m[5] / sy, r21 = m[9] / sy;
	const float r02 = m[2] / sz, r12 = m[6] / sz, r22 = m[10] / sz;
	const float syaw = -r20;
	float pitch, yaw, roll;
	if (syaw > 1.0f - 1e-6f)
	{
		yaw = (float)(M_PI / 2);
		pitch = atan2f (-r12, r11);
		roll = 0.0f;
	}
	else if (syaw < -1.0f + 1e-6f)
	{
		yaw = (float)(-M_PI / 2);
		pitch = atan2f (r12, r11);
		roll = 0.0f;
	}
	else
	{
		yaw = asinf (syaw);
		pitch = atan2f (r21, r22);
		roll = atan2f (r10, r00);
		(void)r01;
		(void)r02;
	}
	// dae_joint_trs builds R = Rz*Ry*Rx, i.e. x=pitch(X), y=yaw(Y), z=roll(Z).
	const float k = (float)(180.0 / M_PI);
	r->x = pitch * k;
	r->y = yaw * k;
	r->z = roll * k;
}

// One triangulated corner straight out of the display list.
typedef struct
{
	int pos, nrm, col, uv;
	int node; // GX matrix-slot id (rigid joint or joints+weight)
} lmm_corner_t;

typedef struct
{
	lmm_corner_t *corners;
	size_t num, cap;
} lmm_corners_t;

static bool lmm_push (lmm_corners_t *c, lmm_corner_t v)
{
	if (c->num >= c->cap)
	{
		const size_t ncap = c->cap ? c->cap * 2 : 256;
		lmm_corner_t *nn = REALLOC (c->corners, ncap * sizeof (*nn));
		if (!nn)
			return false;
		c->corners = nn;
		c->cap = ncap;
	}
	c->corners[c->num++] = v;
	return true;
}

//-opcode fan/strip -> triangle soup, same emission order as the reference.
static bool lmm_emit_tris (lmm_corners_t *out, u8 op, const lmm_corner_t *v, uint n)
{
	if (op == 0x90)
	{
		for (uint i = 0; i + 2 < n; i += 3)
			if (!lmm_push (out, v[i]) || !lmm_push (out, v[i + 1]) || !lmm_push (out, v[i + 2]))
				return false;
		return true;
	}
	if (n < 3)
		return true;
	if (op == 0xa0) // fans: (0, i-1, i) with degenerate rejection
	{
		for (uint i = 0; i < 3 && i < n; i++)
			if (!lmm_push (out, v[i]))
				return false;
		for (uint i = 3; i < n; i++)
		{
			const lmm_corner_t a = v[0], b = v[i - 1], d = v[i];
			if ((a.pos != b.pos || a.nrm != b.nrm || a.uv != b.uv)
				&& (b.pos != d.pos || b.nrm != d.nrm || b.uv != d.uv)
				&& (d.pos != a.pos || d.nrm != a.nrm || d.uv != a.uv))
			{
				if (!lmm_push (out, b) || !lmm_push (out, d) || !lmm_push (out, a))
					return false;
			}
		}
		return true;
	}
	// strips: alternating winding, degenerate rejection
	for (uint i = 2; i < n; i++)
	{
		lmm_corner_t v0 = (i % 2) == 0 ? v[i - 2] : v[i - 1];
		lmm_corner_t v1 = (i % 2) == 0 ? v[i] : v[i - 2];
		lmm_corner_t v2 = (i % 2) == 0 ? v[i - 1] : v[i];
		if ((v0.pos != v1.pos || v0.nrm != v1.nrm || v0.uv != v1.uv)
			&& (v1.pos != v2.pos || v1.nrm != v2.nrm || v1.uv != v2.uv)
			&& (v2.pos != v0.pos || v2.nrm != v0.nrm || v2.uv != v0.uv))
		{
			if (!lmm_push (out, v1) || !lmm_push (out, v2) || !lmm_push (out, v0))
				return false;
		}
	}
	return true;
}

// Walk one packet's display list, appending corners. SHAPE_NBT tells whether
// tangent/binormal indices are present. Returns false on corrupt data.
static bool lmm_walk_packet (const u8 *data, uint size, u32 off, u32 len,
	const u16 *mats, uint n_mats, uint joints, bool has_nrm, bool shape_nbt,
	bool has_col, bool has_uv, lmm_corners_t *out)
{
	if ((u64)off + len > size)
		return false;
	const u8 *p = data + off, *end = p + len;
	while (p < end)
	{
		const u8 op = *p++;
		if (op == 0)
			continue;
		if (op != 0x90 && op != 0x98 && op != 0xa0)
			return false;
		if (p + 2 > end)
			return false;
		const uint n = (uint)p[0] << 8 | p[1];
		p += 2;
		if (!n || n > 65536)
			return false;
		lmm_corner_t *v = MALLOC (n * sizeof (*v));
		if (!v)
			return false;
		bool ok = true;
		for (uint i = 0; i < n && ok; i++)
		{
			// s8 matrix, s8 tex0, s8 tex1, s16 pos [, s16 nrm] [, tan, bin]
			// [, s16 col] [, s16 uv]
			uint need = 3 + 2;
			if (has_nrm)
				need += 2;
			if (shape_nbt)
				need += 4;
			if (has_col)
				need += 2;
			if (has_uv)
				need += 2;
			if ((size_t)(end - p) < need)
			{
				ok = false;
				break;
			}
			const int mi = (int8_t)p[0];
			const int pi = lmm_be16s (p + 3);
			int ni = -1, ci = -1, ti = -1;
			const u8 *q = p + 5;
			if (has_nrm)
			{
				ni = lmm_be16s (q);
				q += 2;
			}
			if (shape_nbt)
				q += 4; // tangent + binormal indices (kept: not exported)
			if (has_col)
			{
				ci = lmm_be16s (q);
				q += 2;
			}
			if (has_uv)
			{
				ti = lmm_be16s (q);
				q += 2;
			}
			p = q;
			int node = -1;
			if (mi != -1)
			{
				if (mi < 0 || mi >= 30 || (uint)(mi / 3) >= n_mats)
				{
					ok = false;
					break;
				}
				node = mats[mi / 3];
			}
			v[i].pos = pi;
			v[i].nrm = ni;
			v[i].col = ci;
			v[i].uv = ti;
			v[i].node = node;
		}
		if (ok)
			ok = lmm_emit_tris (out, op, v, n);
		FREE (v);
		if (!ok)
			return false;
	}
	return true;
}

typedef struct
{
	u8 fmt; // LM format byte
	u16 w, h;
	u32 pix_off, pix_size;
	int gx;
} lmm_tex_t;

model_t *ParseLMMDL (const u8 *data, size_t size)
{
	lmm_hdr_t h;
	if (!lmm_read_hdr (data, (uint)size, &h))
	{
		fprintf (stderr, "DBG lmmdl: hdr reject\n");
		fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
	}

	// Bounds-check every section up front.
	if ((u64)h.node_off + (u64)h.nodes * 16 > size
		|| (u64)h.packet_off + (u64)h.packets * 2 * 32 > size
		|| (u64)h.matrix_off + (u64)h.joints * 48 > size
		|| (u64)h.vert_off + (u64)h.verts * 12 > size
		|| (u64)h.normal_off + (u64)h.normals * 12 > size
		|| (u64)h.colour_off + (u64)h.colours * 4 > size
		|| (u64)h.uv_off + (u64)h.uvs * 8 > size
		|| (u64)h.tex_off + (u64)h.textures * 4 > size
		|| (u64)h.mat_off + (u64)h.materials * 288 > size
		|| (u64)h.samp_off + (u64)h.samplers * 8 > size
		|| (u64)h.shape_off + (u64)h.shapes * 8 > size
		|| (u64)h.elem_off + (u64)h.elements * 4 > size)
		fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
	if (h.weights
		&& ((u64)h.wcnt_off + h.weights > size || (u64)h.weight_off > size || (u64)h.jidx_off > size))
		fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;

	model_t *model = CALLOC (1, sizeof (*model));
	if (!model)
		fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;

	//--- textures: headers first (pixels decoded by DecodeLMMDL) ---
	lmm_tex_t *tex = 0;
	if (h.textures)
	{
		tex = CALLOC (h.textures, sizeof (*tex));
		if (!tex)
		{
			FREE (model);
			fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
		}
		for (uint i = 0; i < h.textures; i++)
		{
			const u32 to = lmm_be32 (data + h.tex_off + i * 4);
			if (!to || (u64)to + 32 > size)
			{
				FREE (tex);
				FreeModel (model);
				fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
			}
			tex[i].fmt = data[to];
			tex[i].w = lmm_be16 (data + to + 2);
			tex[i].h = lmm_be16 (data + to + 4);
			tex[i].gx = lmm_tex_gx (tex[i].fmt);
			if (tex[i].gx < 0 || !tex[i].w || !tex[i].h || tex[i].w > 2048 || tex[i].h > 2048)
			{
				FREE (tex);
				FreeModel (model);
				fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
			}
			tex[i].pix_size = lmm_gx_size ((uint)tex[i].gx, tex[i].w, tex[i].h);
			tex[i].pix_off = to + 32;
			if (!tex[i].pix_size || (u64)tex[i].pix_off + tex[i].pix_size > size)
			{
				FREE (tex);
				FreeModel (model);
				fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
			}
		}
	}

	//--- materials + samplers ---
	model->num_materials = h.materials ? h.materials : 1;
	model->materials = CALLOC (model->num_materials, sizeof (*model->materials));
	if (!model->materials)
	{
		FREE (tex);
		FreeModel (model);
		fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
	}
	for (size_t i = 0; i < model->num_materials; i++)
	{
		material_t *m = model->materials + i;
		snprintf (m->name, sizeof (m->name), "Material%u", (uint)i);
		m->diffuse[0] = m->diffuse[1] = m->diffuse[2] = 1.0f;
		m->diffuse[3] = 1.0f;
		if (!h.materials)
			continue;
		const u8 *mp = data + h.mat_off + i * 288;
		m->diffuse[0] = mp[0] / 255.0f;
		m->diffuse[1] = mp[1] / 255.0f;
		m->diffuse[2] = mp[2] / 255.0f;
		m->diffuse[3] = mp[3] / 255.0f;
		m->has_alpha = mp[6] != 0;
		const uint ntev = mp[7] > 8 ? 8 : mp[7];
		for (uint t = 0; t < ntev && m->num_textures < 8; t++)
		{
			const u8 *sp = mp + 32 + t * 32;
			const uint si = lmm_be16 (sp + 2);
			if (si >= h.samplers)
				continue;
			const u8 *samp = data + h.samp_off + si * 8;
			const uint ti = lmm_be16 (samp);
			if (ti >= h.textures)
				continue;
			const int k = m->num_textures++;
			snprintf (m->textures[k], sizeof (m->textures[k]), "Texture%u.png", ti);
			const u8 wu = samp[4], wv = samp[5];
			m->wrap_s[k] = wu == 0 ? 0 : wu == 2 ? 2 : 1;
			m->wrap_t[k] = wv == 0 ? 0 : wv == 2 ? 2 : 1;
			m->min_filter[k] = m->mag_filter[k] = 1;
			m->texture_coord[k] = 0;
		}
	}

	//--- skeleton: parents from the node graph, worlds from the matrices ---
	model->num_joints = h.joints;
	model->joints = CALLOC (model->num_joints, sizeof (*model->joints));
	float (*worlds)[12] = CALLOC (model->num_joints, sizeof (*worlds));
	if (!model->joints || !worlds)
	{
		FREE (tex);
		FREE (worlds);
		FreeModel (model);
		fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
	}
	// node graph walk from node 0 (child/sibling are relative indices)
	int *parent = MALLOC (h.nodes * sizeof (*parent));
	bool *seen = CALLOC (h.nodes, sizeof (*seen));
	if (!parent || !seen)
	{
		FREE (tex);
		FREE (worlds);
		FREE (parent);
		FREE (seen);
		FreeModel (model);
		fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
	}
	for (uint i = 0; i < h.nodes; i++)
		parent[i] = -2;
	{
		uint stack[8192];
		size_t nst = 0;
		stack[nst++] = 0;
		parent[0] = -1;
		while (nst)
		{
			const uint idx = stack[--nst];
			if (idx >= h.nodes || seen[idx])
				continue;
			seen[idx] = true;
			const u8 *np = data + h.node_off + idx * 16;
			const uint ci = lmm_be16 (np + 2), sib = lmm_be16 (np + 4);
			if (ci && ci < 4096 && idx + ci < h.nodes && parent[idx + ci] == -2)
			{
				parent[idx + ci] = (int)idx;
				if (nst < 8192)
					stack[nst++] = idx + ci;
			}
			if (sib && sib < 4096 && idx + sib < h.nodes && parent[idx + sib] == -2)
			{
				parent[idx + sib] = parent[idx];
				if (nst < 8192)
					stack[nst++] = idx + sib;
			}
		}
	}
	for (size_t j = 0; j < model->num_joints; j++)
	{
		joint_t *jt = model->joints + j;
		const u8 *np = data + h.node_off + j * 16;
		const uint sc = lmm_be16 (np + 8);
		snprintf (jt->name, sizeof (jt->name), "%s%u", sc ? "Mesh" : "Bone", (uint)j);
		jt->parent_idx = parent[j] == -2 ? -1 : parent[j];
		if (jt->parent_idx >= (int)model->num_joints)
			jt->parent_idx = -1;
		float stored[12];
		const u8 *mp = data + h.matrix_off + j * 48;
		for (int k = 0; k < 12; k++)
			stored[k] = lmm_bef32 (mp + k * 4);
		bool finite = true;
		for (int k = 0; k < 12; k++)
			if (!isfinite (stored[k]))
				finite = false;
		if (!finite || !lmm_invert43 (worlds[j], stored))
		{
			static const float id[12]
				= { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
			memcpy (worlds[j], id, sizeof (id));
			memcpy (jt->inverse_bind, id, sizeof (id));
		}
		else
			memcpy (jt->inverse_bind, stored, sizeof (stored));
		jt->has_inverse_bind = 1;
		memcpy (jt->bind, worlds[j], sizeof (worlds[j]));
		jt->scale.x = jt->scale.y = jt->scale.z = 1.0f;
	}
	FREE (parent);
	FREE (seen);
	// local TRS per joint for the node hierarchy
	for (size_t j = 0; j < model->num_joints; j++)
	{
		joint_t *jt = model->joints + j;
		float local[12];
		if (jt->parent_idx >= 0)
		{
			float pinv[12];
			if (!lmm_invert43 (pinv, worlds[jt->parent_idx]))
			{
				memcpy (local, worlds[j], sizeof (local));
			}
			else
				lmm_mul43 (local, pinv, worlds[j]);
		}
		else
			memcpy (local, worlds[j], sizeof (local));
		lmm_decompose (local, &jt->translate, &jt->rotate, &jt->scale);
	}
	FREE (worlds);

	//--- weight table -> node influences ---
	const uint n_slots = h.joints + h.weights;
	model->num_node_influences = n_slots;
	model->node_influences = CALLOC (n_slots ? n_slots : 1, sizeof (*model->node_influences));
	if (!model->node_influences)
	{
		FREE (tex);
		FreeModel (model);
		fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
	}
	for (uint j = 0; j < h.joints; j++)
	{
		node_influence_t *inf = model->node_influences + j;
		inf->weights = MALLOC (sizeof (*inf->weights));
		if (!inf->weights)
		{
			FREE (tex);
			FreeModel (model);
			fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
		}
		inf->num_weights = 1;
		inf->weights[0].bone_idx = (int)j;
		inf->weights[0].weight = 1.0f;
	}
	if (h.weights)
	{
		u32 wpos = h.weight_off, jpos = h.jidx_off;
		for (uint i = 0; i < h.weights; i++)
		{
			const uint cnt = data[h.wcnt_off + i];
			node_influence_t *inf = model->node_influences + h.joints + i;
			if (!cnt || cnt > 16 || (u64)wpos + cnt * 4 > size || (u64)jpos + cnt * 2 > size)
			{
				FREE (tex);
				FreeModel (model);
				fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
			}
			inf->weights = MALLOC (cnt * sizeof (*inf->weights));
			if (!inf->weights)
			{
				FREE (tex);
				FreeModel (model);
				fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
			}
			inf->num_weights = cnt;
			for (uint k = 0; k < cnt; k++)
			{
				inf->weights[k].weight = lmm_bef32 (data + wpos + k * 4);
				inf->weights[k].bone_idx = lmm_be16 (data + jpos + k * 2);
				if (inf->weights[k].bone_idx >= (int)h.joints)
					inf->weights[k].bone_idx = 0;
			}
			wpos += cnt * 4;
			jpos += cnt * 2;
		}
	}

	//--- packet -> shape map (shapes partition the main packet array) ---
	int *packet_shape = 0;
	if (h.packets)
	{
		packet_shape = MALLOC (h.packets * sizeof (*packet_shape));
		if (!packet_shape)
		{
			FREE (tex);
			FreeModel (model);
			fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
		}
		for (uint i = 0; i < h.packets; i++)
			packet_shape[i] = -1;
		for (uint s = 0; s < h.shapes; s++)
		{
			const u8 *shp = data + h.shape_off + s * 8;
			const uint cnt = lmm_be16 (shp + 4), beg = lmm_be16 (shp + 5);
			(void)beg;
			const uint start = lmm_be16 (shp + 6);
			if ((u64)start + cnt > h.packets)
			{
				FREE (tex);
				FREE (packet_shape);
				FreeModel (model);
				fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
			}
			for (uint k = 0; k < cnt; k++)
				packet_shape[start + k] = (int)s;
		}
	}

	//--- meshes: one per draw element ---
	if (!h.elements)
	{
		fprintf (stderr, "DBG lmmdl: no elements\n");
		FREE (tex);
		FREE (packet_shape);
		FreeModel (model);
		fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
	}
	model->num_meshes = h.elements;
	model->meshes = CALLOC (model->num_meshes, sizeof (*model->meshes));
	if (!model->meshes)
	{
		FREE (tex);
		FREE (packet_shape);
		FreeModel (model);
		fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
	}
	const bool has_nrm = h.normals > 0, has_col = h.colours > 0, has_uv = h.uvs > 0;
	for (size_t e = 0; e < model->num_meshes; e++)
	{
		mesh_t *mesh = model->meshes + e;
		snprintf (mesh->name, sizeof (mesh->name), "Mesh%u", (uint)e);
		const u8 *ep = data + h.elem_off + e * 4;
		const uint mi = lmm_be16 (ep), si = lmm_be16 (ep + 2);
		if (mi >= (uint)model->num_materials || si >= h.shapes)
		{
			FREE (tex);
			FREE (packet_shape);
			FreeModel (model);
			fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
		}
		mesh->material_idx = (int)mi;
		const u8 *shp = data + h.shape_off + si * 8;
		const bool nbt = shp[0] > 1;
		const uint pcnt = lmm_be16 (shp + 4), pbeg = lmm_be16 (shp + 6);
		if ((u64)pbeg + pcnt > h.packets)
		{
			FREE (tex);
			FREE (packet_shape);
			FreeModel (model);
			fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
		}
		lmm_corners_t soup = { 0 };
		bool ok = true;
		for (uint k = 0; k < pcnt && ok; k++)
		{
			const uint pi = pbeg + k;
			const u8 *pp = data + h.packet_off + pi * 32;
			const u32 doff = lmm_be32 (pp), dlen = lmm_be32 (pp + 4);
			const uint nm = lmm_be16 (pp + 8);
			u16 mats[10];
			if (nm > 10 || (u64)doff + dlen > size)
				ok = false;
			else
			{
				for (uint m = 0; m < nm; m++)
					mats[m] = lmm_be16 (pp + 10 + m * 2);
				ok = lmm_walk_packet (data, (uint)size, doff, dlen, mats, nm, h.joints,
					has_nrm, nbt, has_col, has_uv, &soup);
			}
		}
		// validate indices before building pools
		if (ok)
			for (size_t c = 0; c < soup.num && ok; c++)
			{
				const lmm_corner_t *cn = soup.corners + c;
				if (cn->pos < 0 || (uint)cn->pos >= h.verts || cn->node < -1
					|| cn->node >= (int)n_slots
					|| (has_nrm && (cn->nrm < 0 || (uint)cn->nrm >= h.normals))
					|| (has_col && (cn->col < 0 || (uint)cn->col >= h.colours))
					|| (has_uv && (cn->uv < 0 || (uint)cn->uv >= h.uvs)))
					ok = false;
			}
		if (!ok || !soup.num || soup.num % 3)
		{
			FREE (soup.corners);
			FREE (tex);
			FREE (packet_shape);
			FreeModel (model);
			fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
		}
		// pools: positions keyed on (pos, node) since one file position may
		// be reused under several matrix slots; other pools by plain index.
		typedef struct
		{
			int idx, node;
		} poskey_t;
		poskey_t *pkeys = MALLOC (soup.num * sizeof (*pkeys));
		int *vpos = MALLOC (soup.num * sizeof (*vpos));
		int *vnrm = MALLOC (soup.num * sizeof (*vnrm));
		int *vcol = MALLOC (soup.num * sizeof (*vcol));
		int *vuv = MALLOC (soup.num * sizeof (*vuv));
		if (!pkeys || !vpos || !vnrm || !vcol || !vuv)
		{
			FREE (pkeys);
			FREE (vpos);
			FREE (vnrm);
			FREE (vcol);
			FREE (vuv);
			FREE (soup.corners);
			FREE (tex);
			FREE (packet_shape);
			FreeModel (model);
			fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
		}
		size_t npos = 0, ncol = 0, nuv = 0;
		mesh->positions = MALLOC (soup.num * sizeof (*mesh->positions));
		mesh->position_node = MALLOC (soup.num * sizeof (*mesh->position_node));
		mesh->normals = 0; // built by the second pass below
		mesh->colors[0] = has_col ? MALLOC (soup.num * sizeof (*mesh->colors[0])) : 0;
		mesh->texcoords = has_uv ? MALLOC (soup.num * sizeof (*mesh->texcoords)) : 0;
		mesh->vertices = MALLOC (soup.num * sizeof (*mesh->vertices));
		if (!mesh->positions || !mesh->position_node || !mesh->vertices
			|| (has_col && !mesh->colors[0])
			|| (has_uv && !mesh->texcoords))
		{
			FREE (pkeys);
			FREE (vpos);
			FREE (vnrm);
			FREE (vcol);
			FREE (vuv);
			FREE (soup.corners);
			FREE (tex);
			FREE (packet_shape);
			FreeModel (model);
			fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
		}
		// world matrix per slot for rigid baking
		float (*slot_world)[12] = CALLOC (n_slots, sizeof (*slot_world));
		if (!slot_world)
		{
			FREE (pkeys);
			FREE (vpos);
			FREE (vnrm);
			FREE (vcol);
			FREE (vuv);
			FREE (soup.corners);
			FREE (tex);
			FREE (packet_shape);
			FreeModel (model);
			fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
		}
		for (size_t j = 0; j < model->num_joints; j++)
			memcpy (slot_world[j], model->joints[j].bind, 12 * sizeof (float));
		for (size_t c = 0; c < soup.num; c++)
		{
			const lmm_corner_t *cn = soup.corners + c;
			size_t pi = npos;
			for (size_t k = 0; k < npos; k++)
				if (pkeys[k].idx == cn->pos && pkeys[k].node == cn->node)
				{
					pi = k;
					break;
				}
			if (pi == npos)
			{
				const u8 *vp = data + h.vert_off + (uint)cn->pos * 12;
				float x = lmm_bef32 (vp), y = lmm_bef32 (vp + 4), z = lmm_bef32 (vp + 8);
				if (cn->node >= 0 && (uint)cn->node < h.joints)
				{
					// rigid: bake joint world (reference behaviour)
					lmm_xform_pos (slot_world[cn->node], &x, &y, &z);
				}
				pkeys[npos].idx = cn->pos;
				pkeys[npos].node = cn->node;
				mesh->positions[npos].x = x;
				mesh->positions[npos].y = y;
				mesh->positions[npos].z = z;
				mesh->position_node[npos] = cn->node;
				npos++;
			}
			vpos[c] = (int)pi;
			vnrm[c] = -1;
			if (has_col && cn->col >= 0)
			{
				const u8 *cp = data + h.colour_off + (uint)cn->col * 4;
				color4_t col = { cp[0] / 255.0f, cp[1] / 255.0f, cp[2] / 255.0f,
					cp[3] / 255.0f };
				size_t ci = ncol;
				for (size_t k = 0; k < ncol; k++)
					if (mesh->colors[0][k].r == col.r && mesh->colors[0][k].g == col.g
						&& mesh->colors[0][k].b == col.b && mesh->colors[0][k].a == col.a)
					{
						ci = k;
						break;
					}
				if (ci == ncol)
					mesh->colors[0][ncol++] = col;
				vcol[c] = (int)ci;
			}
			else
				vcol[c] = -1;
			if (has_uv && cn->uv >= 0)
			{
				const u8 *tp = data + h.uv_off + (uint)cn->uv * 8;
				vec2_t uv = { lmm_bef32 (tp), lmm_bef32 (tp + 4) };
				size_t ti = nuv;
				for (size_t k = 0; k < nuv; k++)
					if (mesh->texcoords[k].u == uv.u && mesh->texcoords[k].v == uv.v)
					{
						ti = k;
						break;
					}
				if (ti == nuv)
					mesh->texcoords[nuv++] = uv;
				vuv[c] = (int)ti;
			}
			else
				vuv[c] = -1;
		}
		// normals: second pass over corners (rigid-baked, deduped).
		{
			vec3_t *uniq = MALLOC ((soup.num + 1) * sizeof (*uniq));
			size_t nu = 0;
			if (!uniq)
			{
				FREE (pkeys);
				FREE (vpos);
				FREE (vnrm);
				FREE (vcol);
				FREE (vuv);
				FREE (soup.corners);
				FREE (tex);
				FREE (packet_shape);
				FreeModel (model);
				fprintf (stderr, "DBG lmmdl: parse fail %d\n", __LINE__); return 0;
			}
			if (has_nrm)
			{
				for (size_t c = 0; c < soup.num; c++)
				{
					const lmm_corner_t *cn = soup.corners + c;
					if (cn->nrm < 0)
						continue;
					const u8 *rp = data + h.normal_off + (uint)cn->nrm * 12;
					vec3_t v = { lmm_bef32 (rp), lmm_bef32 (rp + 4), lmm_bef32 (rp + 8) };
					if (cn->node >= 0 && (uint)cn->node < h.joints)
					{
						// same rigid bake as positions
						float x = v.x, y = v.y, z = v.z;
						lmm_xform_nrm (slot_world[cn->node], &x, &y, &z);
						v.x = x;
						v.y = y;
						v.z = z;
					}
					size_t f = nu;
					for (size_t k = 0; k < nu; k++)
						if (uniq[k].x == v.x && uniq[k].y == v.y && uniq[k].z == v.z)
						{
							f = k;
							break;
						}
					if (f == nu)
						uniq[nu++] = v;
					vnrm[c] = (int)f;
				}
				FREE (mesh->normals);
				mesh->normals = nu ? MALLOC (nu * sizeof (*mesh->normals)) : 0;
				if (nu && mesh->normals)
					memcpy (mesh->normals, uniq, nu * sizeof (*mesh->normals));
				mesh->num_normals = nu;
			}
			FREE (uniq);
		}
		mesh->num_positions = npos;
		mesh->num_colors[0] = ncol;
		if (!has_col)
		{
			FREE (mesh->colors[0]);
			mesh->colors[0] = 0;
		}
		mesh->num_texcoords = nuv;
		if (!has_uv)
		{
			FREE (mesh->texcoords);
			mesh->texcoords = 0;
		}
		if (!has_nrm)
		{
			FREE (mesh->normals);
			mesh->normals = 0;
			mesh->num_normals = 0;
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
			v->color_idx[0] = vcol[c];
			v->color_idx[1] = -1;
			for (int k = 0; k < 7; k++)
				v->extra_texcoord_idx[k] = -1;
		}
		FREE (pkeys);
		FREE (vpos);
		FREE (vnrm);
		FREE (vcol);
		FREE (vuv);
		FREE (soup.corners);
		FREE (slot_world);
	}
	FREE (tex);
	FREE (packet_shape);
	fprintf (stderr, "DBG lmmdl: parse ok meshes=%u\n", (uint)model->num_meshes);
	return model;
}

enumError DecodeLMMDL (const u8 *data, uint size, ccp out_path)
{
	lmm_hdr_t h;
	if (!lmm_read_hdr (data, size, &h))
		return ERR_NOTHING_TO_DO;
	model_t *model = ParseLMMDL (data, size);
	if (!model)
		return ERR_NOTHING_TO_DO;

	// sibling PNGs for the embedded GX textures
	for (uint i = 0; i < h.textures; i++)
	{
		const u32 to = lmm_be32 (data + h.tex_off + i * 4);
		const u8 fmt = data[to];
		const uint w = lmm_be16 (data + to + 2), hh = lmm_be16 (data + to + 4);
		const int gx = lmm_tex_gx (fmt);
		const uint need = lmm_gx_size ((uint)gx, w, hh);
		u8 *rgba = 0;
		if (!DecodeGXTexture_RGBA (&rgba, w, hh, (uint)gx, data + to + 32, need, 0, 0, 0))
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
	fprintf (stderr, "DBG lmmdl: export rc=%d\n", rc);
	FreeModel (model);
	return rc == 0 ? ERR_OK : ERR_CANT_CREATE;
}

// ---- encoder ----

typedef struct
{
	float x, y, z;
} f3_t;

typedef struct
{
	float u, v;
} f2_t;

// dedup helpers with exact float equality (same rule as the reference)
static size_t lmm_find3 (const f3_t *a, size_t n, f3_t v)
{
	for (size_t i = 0; i < n; i++)
		if (a[i].x == v.x && a[i].y == v.y && a[i].z == v.z)
			return i;
	return n;
}

static size_t lmm_find2 (const f2_t *a, size_t n, f2_t v)
{
	for (size_t i = 0; i < n; i++)
		if (a[i].u == v.u && a[i].v == v.v)
			return i;
	return n;
}

typedef struct
{
	uint jcount;
	int *joints; // file node ids used by this packet (rigid or smooth slot)
	float *weights; // parallel smooth weights (0 for rigid use)
} lmm_slotset_t;

enumError EncodeLMMDL (const model_t *model, u8 **out, uint *out_size)
{
	if (!out || !out_size || !model || !model->num_meshes)
		return ERR_INVALID_DATA;
	const size_t nm = model->num_meshes;
	const size_t nj = model->num_joints;
	if (!nj || nj > LMMDL_MAX_NODES || nm > LMMDL_MAX_MESHES)
		return ERR_INVALID_DATA;

	// world + inverse-bind per joint (fall back to TRS-composed binds)
	float (*worlds)[12] = CALLOC (nj, sizeof (*worlds));
	float (*ibinds)[12] = CALLOC (nj, sizeof (*ibinds));
	if (!worlds || !ibinds)
	{
		FREE (worlds);
		FREE (ibinds);
		return ERR_OUT_OF_MEMORY;
	}
	for (size_t j = 0; j < nj; j++)
	{
		const joint_t *jt = model->joints + j;
		if (jt->has_inverse_bind)
		{
			memcpy (ibinds[j], jt->inverse_bind, sizeof (ibinds[j]));
			if (!lmm_invert43 (worlds[j], ibinds[j]))
			{
				FREE (worlds);
				FREE (ibinds);
				return ERR_INVALID_DATA;
			}
		}
		else
		{
			// compose local TRS like dae_joint_trs (ZYX degrees)
			const double dx = jt->rotate.x * (M_PI / 180.0);
			const double dy = jt->rotate.y * (M_PI / 180.0);
			const double dz = jt->rotate.z * (M_PI / 180.0);
			const float cx = cosf ((float)dx), sx = sinf ((float)dx);
			const float cy = cosf ((float)dy), sy = sinf ((float)dy);
			const float cz = cosf ((float)dz), sz = sinf ((float)dz);
			float local[12] = { cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx, 0,
				sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx, 0, -sy, cy * sx,
				cy * cx, 0 };
			for (int r = 0; r < 3; r++)
			{
				local[r * 4] *= jt->scale.x;
				local[r * 4 + 1] *= jt->scale.y;
				local[r * 4 + 2] *= jt->scale.z;
			}
			local[3] = jt->translate.x;
			local[7] = jt->translate.y;
			local[11] = jt->translate.z;
			if (jt->parent_idx >= 0 && (size_t)jt->parent_idx < nj)
				lmm_mul43 (worlds[j], worlds[jt->parent_idx], local);
			else
				memcpy (worlds[j], local, sizeof (worlds[j]));
			if (!lmm_invert43 (ibinds[j], worlds[j]))
			{
				FREE (worlds);
				FREE (ibinds);
				return ERR_INVALID_DATA;
			}
		}
	}

	// pools
	f3_t *positions = 0, *normals = 0;
	f2_t *uvs = 0;
	u8 (*colours)[4] = 0;
	size_t npos = 0, cap_pos = 0, nnrm = 0, cap_nrm = 0, nuv = 0, cap_uv = 0, ncol = 0,
		   cap_col = 0;

	// per-mesh packet build: one packet per mesh (rigid or single smooth
	// node per position keeps every packet within the 10-slot limit for
	// sane inputs; meshes that genuinely need more slots are split).
	typedef struct
	{
		u8 *blob;
		uint len;
		u8 *lod;
		uint lod_len, lod_off;
		u16 slots[10];
		uint nslots;
		uint mesh, mat;
	} lmm_packet_t;
	lmm_packet_t *packets = 0;
	size_t npack = 0, cap_pack = 0;
	uint total_tris = 0;

	// texture/material/sampler tables from the model
	const size_t nimg = model->num_images;
	size_t nmat = model->num_materials ? model->num_materials : 1;

	// file-level attribute presence: every packet shares one vertex layout
	// (the decoder keys field presence off the file counts), so pool and
	// blob emission use these globals, with 0/white fallbacks for corners
	// that lack an attribute.
	bool g_hn = false, g_hc = false, g_ht = false;
	for (size_t m0 = 0; m0 < nm; m0++)
	{
		if (model->meshes[m0].normals && model->meshes[m0].num_normals > 0)
			g_hn = true;
		if (model->meshes[m0].colors[0] && model->meshes[m0].num_colors[0] > 0)
			g_hc = true;
		if (model->meshes[m0].texcoords && model->meshes[m0].num_texcoords > 0)
			g_ht = true;
	}

	for (size_t m = 0; m < nm; m++)
	{
		const mesh_t *mesh = model->meshes + m;
		if (!mesh->num_vertices || mesh->num_vertices % 3)
		{
			FREE (worlds);
			FREE (ibinds);
			FREE (positions);
			FREE (normals);
			FREE (uvs);
			FREE (colours);
			for (size_t k = 0; k < npack; k++)
				FREE (packets[k].blob);
			FREE (packets);
			return ERR_INVALID_DATA;
		}
		const int mati = mesh->material_idx >= 0 && (size_t)mesh->material_idx < nmat
			? mesh->material_idx
			: 0;

		// packet slot assignment: unique position_node values in this mesh
		int slots[10];
		uint nslots = 0;
		for (size_t c = 0; c < mesh->num_vertices; c++)
		{
			const int pi = mesh->vertices[c].position_idx;
			int node = (pi >= 0 && (size_t)pi < mesh->num_positions) ? mesh->position_node[pi]
																	 : 0;
			if (node < 0 || (size_t)node >= nj)
				node = 0;
			uint f = nslots;
			for (uint k = 0; k < nslots; k++)
				if (slots[k] == node)
				{
					f = k;
					break;
				}
			if (f == nslots)
			{
				if (nslots >= 10)
				{
					// slot overflow: fall back to node 0 (documented)
					node = 0;
					f = 0;
					for (uint k = 0; k < nslots; k++)
						if (slots[k] == 0)
						{
							f = k;
							break;
						}
				}
				else
					slots[nslots++] = node;
			}
		}

		// worst case blob: 3 + verts*(3+3*2+2+2)
		const size_t worst = 3 + mesh->num_vertices * 16;
		u8 *blob = MALLOC (worst ? worst : 1);
		if (!blob)
		{
			FREE (worlds);
			FREE (ibinds);
			FREE (positions);
			FREE (normals);
			FREE (uvs);
			FREE (colours);
			for (size_t k = 0; k < npack; k++)
				FREE (packets[k].blob);
			FREE (packets);
			return ERR_OUT_OF_MEMORY;
		}
		u8 *bp = blob;
		*bp++ = 0x90; // triangles
		lmm_wr16 (bp, (u16)mesh->num_vertices);
		bp += 2;
		const bool has_n = g_hn, has_c = g_hc, has_t = g_ht;
		for (size_t c = 0; c < mesh->num_vertices; c++)
		{
			const vertex_t *v = mesh->vertices + c;
			const int pi = v->position_idx;
			int node = (pi >= 0 && (size_t)pi < mesh->num_positions) ? mesh->position_node[pi]
																	 : 0;
			if (node < 0 || (size_t)node >= nj)
				node = 0;
			uint slot = 0;
			for (uint k = 0; k < nslots; k++)
				if (slots[k] == node)
				{
					slot = k;
					break;
				}
			// file-space position = inverse-bind x world position
			float x = mesh->positions[pi].x, y = mesh->positions[pi].y, z = mesh->positions[pi].z;
			lmm_xform_pos (ibinds[node], &x, &y, &z);
			f3_t P = { x, y, z };
			size_t found = lmm_find3 (positions, npos, P);
			if (found == npos)
			{
				if (npos >= cap_pos)
				{
					const size_t nc = cap_pos ? cap_pos * 2 : 1024;
					f3_t *nn = REALLOC (positions, nc * sizeof (*nn));
					if (!nn)
					{
						FREE (blob);
						FREE (worlds);
						FREE (ibinds);
						FREE (positions);
						FREE (normals);
						FREE (uvs);
						FREE (colours);
						for (size_t k = 0; k < npack; k++)
							FREE (packets[k].blob);
						FREE (packets);
						return ERR_OUT_OF_MEMORY;
					}
					positions = nn;
					cap_pos = nc;
				}
				positions[npos++] = P;
			}
			float nx = 0, ny = 0, nz = 0;
			if (has_n && mesh->normals && v->normal_idx >= 0
				&& (size_t)v->normal_idx < mesh->num_normals)
			{
				nx = mesh->normals[v->normal_idx].x;
				ny = mesh->normals[v->normal_idx].y;
				nz = mesh->normals[v->normal_idx].z;
				lmm_xform_nrm (ibinds[node], &nx, &ny, &nz);
			}
			f3_t N = { nx, ny, nz };
			size_t fn = has_n ? lmm_find3 (normals, nnrm, N) : 0;
			if (has_n && fn == nnrm)
			{
				if (nnrm >= cap_nrm)
				{
					const size_t nc = cap_nrm ? cap_nrm * 2 : 1024;
					f3_t *nn = REALLOC (normals, nc * sizeof (*nn));
					if (!nn)
					{
						FREE (blob);
						FREE (worlds);
						FREE (ibinds);
						FREE (positions);
						FREE (normals);
						FREE (uvs);
						FREE (colours);
						for (size_t k = 0; k < npack; k++)
							FREE (packets[k].blob);
						FREE (packets);
						return ERR_OUT_OF_MEMORY;
					}
					normals = nn;
					cap_nrm = nc;
				}
				normals[nnrm++] = N;
			}
			u8 col[4] = { 255, 255, 255, 255 };
			if (has_c && mesh->colors[0] && v->color_idx[0] >= 0
				&& (size_t)v->color_idx[0] < mesh->num_colors[0])
			{
				const color4_t *cc = mesh->colors[0] + v->color_idx[0];
				col[0] = (u8)(cc->r * 255.0f);
				col[1] = (u8)(cc->g * 255.0f);
				col[2] = (u8)(cc->b * 255.0f);
				col[3] = (u8)(cc->a * 255.0f);
			}
			size_t fc = ncol;
			if (has_c)
			{
				for (size_t k = 0; k < ncol; k++)
					if (!memcmp (colours[k], col, 4))
					{
						fc = k;
						break;
					}
				if (fc == ncol)
				{
					if (ncol >= cap_col)
					{
						const size_t nc = cap_col ? cap_col * 2 : 256;
						u8(*nn)[4] = REALLOC (colours, nc * sizeof (*nn));
						if (!nn)
						{
							FREE (blob);
							FREE (worlds);
							FREE (ibinds);
							FREE (positions);
							FREE (normals);
							FREE (uvs);
							FREE (colours);
							for (size_t k = 0; k < npack; k++)
								FREE (packets[k].blob);
							FREE (packets);
							return ERR_OUT_OF_MEMORY;
						}
						colours = nn;
						cap_col = nc;
					}
					memcpy (colours[ncol++], col, 4);
				}
			}
			f2_t T = { 0, 0 };
			if (has_t && mesh->texcoords && v->texcoord_idx >= 0
				&& (size_t)v->texcoord_idx < mesh->num_texcoords)
			{
				T.u = mesh->texcoords[v->texcoord_idx].u;
				T.v = mesh->texcoords[v->texcoord_idx].v;
			}
			size_t ft = has_t ? lmm_find2 (uvs, nuv, T) : 0;
			if (has_t && ft == nuv)
			{
				if (nuv >= cap_uv)
				{
					const size_t nc = cap_uv ? cap_uv * 2 : 1024;
					f2_t *nn = REALLOC (uvs, nc * sizeof (*nn));
					if (!nn)
					{
						FREE (blob);
						FREE (worlds);
						FREE (ibinds);
						FREE (positions);
						FREE (normals);
						FREE (uvs);
						FREE (colours);
						for (size_t k = 0; k < npack; k++)
							FREE (packets[k].blob);
						FREE (packets);
						return ERR_OUT_OF_MEMORY;
					}
					uvs = nn;
					cap_uv = nc;
				}
				uvs[nuv++] = T;
			}
			*bp++ = (u8)(slot * 3);
			*bp++ = (u8)(slot * 3);
			*bp++ = (u8)(slot * 3);
			lmm_wr16 (bp, (u16)found);
			bp += 2;
			if (has_n)
			{
				lmm_wr16 (bp, (u16)fn);
				bp += 2;
			}
			if (has_c)
			{
				lmm_wr16 (bp, (u16)fc);
				bp += 2;
			}
			if (has_t)
			{
				lmm_wr16 (bp, (u16)ft);
				bp += 2;
			}
		}
		if (npack >= cap_pack)
		{
			const size_t nc = cap_pack ? cap_pack * 2 : 16;
			lmm_packet_t *nn = REALLOC (packets, nc * sizeof (*nn));
			if (!nn)
			{
				FREE (blob);
				FREE (worlds);
				FREE (ibinds);
				FREE (positions);
				FREE (normals);
				FREE (uvs);
				FREE (colours);
				for (size_t k = 0; k < npack; k++)
					FREE (packets[k].blob);
				FREE (packets);
				return ERR_OUT_OF_MEMORY;
			}
			packets = nn;
			cap_pack = nc;
		}
		lmm_packet_t *pk = packets + npack++;
		pk->blob = blob;
		pk->len = (uint)(bp - blob);
		pk->lod = 0;
		pk->lod_len = 0;
		pk->lod_off = 0;
		pk->nslots = nslots;
		for (uint k = 0; k < nslots; k++)
			pk->slots[k] = (u16)slots[k];
		pk->mesh = (uint)m;
		pk->mat = (uint)mati;
		total_tris += (uint)(mesh->num_vertices / 3);
	}
	if (npos > 65535 || nnrm > 65535 || nuv > 65535 || ncol > 65535 || npack > 65535)
	{
		FREE (worlds);
		FREE (ibinds);
		FREE (positions);
		FREE (normals);
		FREE (uvs);
		FREE (colours);
		for (size_t k = 0; k < npack; k++)
			FREE (packets[k].blob);
		FREE (packets);
		return ERR_INVALID_DATA;
	}
	const bool has_n = nnrm > 0;
	const uint ntex = (uint)nimg;

	// LOD shadow blobs from the final pools (matrix, position, zero normal
	// per corner -- the reference zeroes LOD normals on re-encode too).
	for (size_t k = 0; k < npack; k++)
	{
		const mesh_t *lmesh = model->meshes + packets[k].mesh;
		const size_t vc = lmesh->num_vertices;
		u8 *lb = MALLOC (3 + vc * 4);
		if (!lb)
		{
			FREE (worlds);
			FREE (ibinds);
			FREE (positions);
			FREE (normals);
			FREE (uvs);
			FREE (colours);
			for (size_t q = 0; q < npack; q++)
			{
				FREE (packets[q].blob);
				FREE (packets[q].lod);
			}
			FREE (packets);
			return ERR_OUT_OF_MEMORY;
		}
		u8 *qp = lb;
		*qp++ = 0x90;
		lmm_wr16 (qp, (u16)vc);
		qp += 2;
		for (size_t c = 0; c < vc; c++)
		{
			const vertex_t *vv = lmesh->vertices + c;
			const int pi = vv->position_idx;
			int node = (pi >= 0 && (size_t)pi < lmesh->num_positions)
				? lmesh->position_node[pi]
				: 0;
			if (node < 0 || (size_t)node >= nj)
				node = 0;
			uint slot = 0;
			for (uint s2 = 0; s2 < packets[k].nslots; s2++)
				if (packets[k].slots[s2] == (u16)node)
				{
					slot = s2;
					break;
				}
			float x = lmesh->positions[pi].x, y = lmesh->positions[pi].y,
				  z = lmesh->positions[pi].z;
			lmm_xform_pos (ibinds[node], &x, &y, &z);
			f3_t P = { x, y, z };
			const size_t fi = lmm_find3 (positions, npos, P);
			*qp++ = (u8)(slot * 3);
			lmm_wr16 (qp, (u16)fi);
			qp += 2;
			*qp++ = 0;
		}
		packets[k].lod = lb;
		packets[k].lod_len = (uint)(qp - lb);
	}

	// node graph: relative child/sibling offsets from parent links
	int *first_child = MALLOC (nj * sizeof (*first_child));
	int *next_sib = MALLOC (nj * sizeof (*next_sib));
	if (!first_child || !next_sib)
	{
		FREE (first_child);
		FREE (next_sib);
		FREE (worlds);
		FREE (ibinds);
		FREE (positions);
		FREE (normals);
		FREE (uvs);
		FREE (colours);
		for (size_t k = 0; k < npack; k++)
		{
			FREE (packets[k].blob);
			FREE (packets[k].lod);
		}
		FREE (packets);
		return ERR_OUT_OF_MEMORY;
	}
	for (size_t j = 0; j < nj; j++)
	{
		first_child[j] = -1;
		next_sib[j] = -1;
	}
	for (size_t j = 0; j < nj; j++)
	{
		const int p = model->joints[j].parent_idx;
		if (p >= 0 && (size_t)p < nj)
		{
			next_sib[j] = first_child[p];
			first_child[p] = (int)j;
		}
	}

	// layout: header + packet blobs (32-aligned) + textures + materials +
	// samplers + shapes + elements + packet structs + tex offsets +
	// positions + normals + colours + uvs + nodes + matrices (+ no weights)
	uint *blob_off = MALLOC (npack * sizeof (*blob_off));
	uint *blob_pad = MALLOC (npack * sizeof (*blob_pad));
	if (!blob_off || !blob_pad)
	{
		FREE (blob_off);
		FREE (blob_pad);
		FREE (first_child);
		FREE (next_sib);
		FREE (worlds);
		FREE (ibinds);
		FREE (positions);
		FREE (normals);
		FREE (uvs);
		FREE (colours);
		for (size_t k = 0; k < npack; k++)
		{
			FREE (packets[k].blob);
			FREE (packets[k].lod);
		}
		FREE (packets);
		return ERR_OUT_OF_MEMORY;
	}
	uint cur = 128;
	for (size_t k = 0; k < npack; k++)
	{
		blob_off[k] = cur;
		blob_pad[k] = (packets[k].len + 31) & ~31u;
		cur += blob_pad[k];
	}
	// LOD shadow blobs follow the main blobs, then Align(32)
	for (size_t k = 0; k < npack; k++)
	{
		packets[k].lod_off = cur;
		cur += (packets[k].lod_len + 31) & ~31u;
	}
	cur = (cur + 31) & ~31u; // Align(32) after packets
	const uint texhdr_off = cur;
	// texture headers: 32 bytes each inline with pixels (CMPR zero-filled)
	uint *tex_pix_size = 0, *tex_w = 0, *tex_h = 0;
	if (ntex)
	{
		tex_pix_size = MALLOC (ntex * sizeof (*tex_pix_size));
		tex_w = MALLOC (ntex * sizeof (*tex_w));
		tex_h = MALLOC (ntex * sizeof (*tex_h));
		if (!tex_pix_size || !tex_w || !tex_h)
		{
			FREE (tex_pix_size);
			FREE (tex_w);
			FREE (tex_h);
			FREE (blob_off);
			FREE (blob_pad);
			FREE (first_child);
			FREE (next_sib);
			FREE (worlds);
			FREE (ibinds);
			FREE (positions);
			FREE (normals);
			FREE (uvs);
			FREE (colours);
			for (size_t k = 0; k < npack; k++)
			{
				FREE (packets[k].blob);
				FREE (packets[k].lod);
			}
			FREE (packets);
			return ERR_OUT_OF_MEMORY;
		}
		for (uint i = 0; i < ntex; i++)
		{
			// dimensions from the PNG IHDR when available, else 8x8
			uint w = 8, hh = 8;
			const model_image_t *im = model->images + i;
			if (im->size >= 24 && !memcmp (im->data, "\x89PNG\r\n\x1a\n", 8)
				&& !memcmp (im->data + 12, "IHDR", 4))
			{
				w = (uint)im->data[16] << 24 | (uint)im->data[17] << 16
					| (uint)im->data[18] << 8 | im->data[19];
				hh = (uint)im->data[20] << 24 | (uint)im->data[21] << 16
					| (uint)im->data[22] << 8 | im->data[23];
				if (!w || !hh || w > 2048 || hh > 2048)
				{
					w = 8;
					hh = 8;
				}
			}
			tex_w[i] = w;
			tex_h[i] = hh;
			tex_pix_size[i] = lmm_gx_size (14, w, hh);
			cur += 32 + tex_pix_size[i];
		}
		cur = (cur + 15) & ~15u; // Align(16)
	}
	const uint mat_off = cur;
	cur += (uint)nmat * 288;
	// samplers: one per material texture layer (min 1 per material)
	uint nsamp = 0;
	for (size_t i = 0; i < nmat; i++)
	{
		const material_t *mt = model->num_materials > i ? model->materials + i : 0;
		const uint nl = mt && mt->num_textures > 0 ? (uint)mt->num_textures : 1;
		nsamp += nl > 8 ? 8 : nl;
	}
	const uint samp_off = cur;
	cur += nsamp * 8;
	const uint shape_off = cur;
	cur += (uint)nm * 8;
	const uint elem_off = cur;
	cur += (uint)nm * 4;
	const uint packstruct_off = cur;
	cur += (uint)npack * 2 * 32;
	const uint texarr_off = cur;
	cur += ntex * 4;
	const uint pos_off = cur;
	cur += (uint)npos * 12;
	const uint nrm_off = cur;
	cur += (uint)nnrm * 12;
	const uint col_off = cur;
	cur += (uint)ncol * 4;
	const uint uv_off = cur;
	cur += (uint)nuv * 8;
	const uint node_off = cur;
	cur += (uint)nj * 16;
	const uint matx_off = cur;
	cur += (uint)nj * 48;
	const uint woff = cur; // empty weight tables point here

	u8 *buf = CALLOC (1, cur ? cur : 1);
	if (!buf)
	{
		FREE (tex_pix_size);
		FREE (tex_w);
		FREE (tex_h);
		FREE (blob_off);
		FREE (blob_pad);
		FREE (first_child);
		FREE (next_sib);
		FREE (worlds);
		FREE (ibinds);
		FREE (positions);
		FREE (normals);
		FREE (uvs);
		FREE (colours);
		for (size_t k = 0; k < npack; k++)
		{
			FREE (packets[k].blob);
			FREE (packets[k].lod);
		}
		FREE (packets);
		return ERR_OUT_OF_MEMORY;
	}

	lmm_wr32 (buf, LMMDL_MAGIC);
	lmm_wr16 (buf + 4, (u16)total_tris);
	lmm_wr16 (buf + 8, (u16)nj);
	lmm_wr16 (buf + 10, (u16)npack);
	lmm_wr16 (buf + 12, 0); // weights: rigid-only output
	lmm_wr16 (buf + 14, (u16)nj);
	lmm_wr16 (buf + 16, (u16)npos);
	lmm_wr16 (buf + 18, (u16)nnrm);
	lmm_wr16 (buf + 20, (u16)ncol);
	lmm_wr16 (buf + 22, (u16)nuv);
	lmm_wr16 (buf + 32, (u16)ntex);
	lmm_wr16 (buf + 36, (u16)nsamp);
	lmm_wr16 (buf + 38, (u16)nm);
	lmm_wr16 (buf + 40, (u16)nmat);
	lmm_wr16 (buf + 42, (u16)nm);
	lmm_wr32 (buf + 48, node_off);
	lmm_wr32 (buf + 52, packstruct_off);
	lmm_wr32 (buf + 56, matx_off);
	lmm_wr32 (buf + 60, woff);
	lmm_wr32 (buf + 64, woff);
	lmm_wr32 (buf + 68, woff);
	lmm_wr32 (buf + 72, pos_off);
	lmm_wr32 (buf + 76, nrm_off);
	lmm_wr32 (buf + 80, col_off);
	lmm_wr32 (buf + 84, uv_off);
	lmm_wr32 (buf + 96, texarr_off);
	lmm_wr32 (buf + 104, mat_off);
	lmm_wr32 (buf + 108, samp_off);
	lmm_wr32 (buf + 112, shape_off);
	lmm_wr32 (buf + 116, elem_off);

	for (size_t k = 0; k < npack; k++)
	{
		memcpy (buf + blob_off[k], packets[k].blob, packets[k].len);
		memcpy (buf + packets[k].lod_off, packets[k].lod, packets[k].lod_len);
	}

	uint toff = texhdr_off;
	for (uint i = 0; i < ntex; i++)
	{
		lmm_wr32 (buf + texarr_off + i * 4, toff);
		buf[toff] = 0x0a; // CMPR
		buf[toff + 1] = 0;
		lmm_wr16 (buf + toff + 2, (u16)tex_w[i]);
		lmm_wr16 (buf + toff + 4, (u16)tex_h[i]);
		// 26 reserved bytes stay zero; pixels stay zero (see header note)
		toff += 32 + tex_pix_size[i];
	}

	uint samp_idx = 0;
	for (size_t i = 0; i < nmat; i++)
	{
		u8 *mp = buf + mat_off + i * 288;
		const material_t *mt = model->num_materials > i ? model->materials + i : 0;
		if (mt)
		{
			mp[0] = (u8)(mt->diffuse[0] * 255.0f);
			mp[1] = (u8)(mt->diffuse[1] * 255.0f);
			mp[2] = (u8)(mt->diffuse[2] * 255.0f);
			mp[3] = (u8)(mt->diffuse[3] * 255.0f);
			mp[6] = mt->has_alpha ? 1 : 0;
		}
		else
		{
			mp[0] = mp[1] = mp[2] = mp[3] = 255;
		}
		uint nl = mt && mt->num_textures > 0 ? (uint)mt->num_textures : 1;
		if (nl > 8)
			nl = 8;
		mp[7] = (u8)nl;
		for (uint t = 0; t < nl; t++)
		{
			u8 *tp = mp + 32 + t * 32;
			int img = -1;
			if (mt && t < (uint)mt->num_textures && mt->textures[t][0])
			{
				// match "TextureN.png" / "TextureN" / bare names to images
				for (uint g = 0; g < ntex; g++)
				{
					char base[64];
					snprintf (base, sizeof (base), "Texture%u", g);
					if (!strcmp (mt->textures[t], model->images[g].name)
						|| strstr (mt->textures[t], base))
					{
						img = (int)g;
						break;
					}
				}
				if (img < 0 && ntex)
					img = 0;
			}
			lmm_wr16 (tp + 2, (u16)(samp_idx + t));
			u8 *sp = buf + samp_off + (samp_idx + t) * 8;
			lmm_wr16 (sp, img >= 0 ? (u16)img : 0xffff);
			lmm_wr16 (sp + 2, 0xffff);
			sp[4] = 1;
			sp[5] = 2;
			sp[6] = sp[7] = 0;
		}
		samp_idx += nl;
	}

	for (size_t m = 0; m < nm; m++)
	{
		u8 *shp = buf + shape_off + m * 8;
		shp[0] = has_n ? 1 : 0;
		shp[2] = 0x26;
		lmm_wr16 (shp + 4, 1);
		lmm_wr16 (shp + 6, (u16)m);
		u8 *ep = buf + elem_off + m * 4;
		lmm_wr16 (ep, (u16)packets[m].mat);
		lmm_wr16 (ep + 2, (u16)m);
	}

	for (size_t k = 0; k < npack; k++)
	{
		// main packet struct
		u8 *pp = buf + packstruct_off + k * 32;
		lmm_wr32 (pp, blob_off[k]);
		lmm_wr32 (pp + 4, packets[k].len);
		lmm_wr16 (pp + 6, 2);
		lmm_wr16 (pp + 8, (u16)packets[k].nslots);
		for (uint s = 0; s < packets[k].nslots; s++)
			lmm_wr16 (pp + 10 + s * 2, packets[k].slots[s]);
		for (uint s = packets[k].nslots; s < 10; s++)
			lmm_wr16 (pp + 10 + s * 2, 0);
		// LOD shadow packet struct (blob copied with the main blobs above)
		u8 *lp = buf + packstruct_off + (npack + k) * 32;
		lmm_wr32 (lp, packets[k].lod_off);
		lmm_wr32 (lp + 4, packets[k].lod_len);
		lmm_wr16 (lp + 6, 2);
		lmm_wr16 (lp + 8, (u16)packets[k].nslots);
		for (uint s = 0; s < packets[k].nslots; s++)
			lmm_wr16 (lp + 10 + s * 2, packets[k].slots[s]);
		for (uint s = packets[k].nslots; s < 10; s++)
			lmm_wr16 (lp + 10 + s * 2, 0);
	}

	for (size_t i = 0; i < npos; i++)
	{
		lmm_wrf32 (buf + pos_off + i * 12, positions[i].x);
		lmm_wrf32 (buf + pos_off + i * 12 + 4, positions[i].y);
		lmm_wrf32 (buf + pos_off + i * 12 + 8, positions[i].z);
	}
	for (size_t i = 0; i < nnrm; i++)
	{
		lmm_wrf32 (buf + nrm_off + i * 12, normals[i].x);
		lmm_wrf32 (buf + nrm_off + i * 12 + 4, normals[i].y);
		lmm_wrf32 (buf + nrm_off + i * 12 + 8, normals[i].z);
	}
	for (size_t i = 0; i < ncol; i++)
		memcpy (buf + col_off + i * 4, colours[i], 4);
	for (size_t i = 0; i < nuv; i++)
	{
		lmm_wrf32 (buf + uv_off + i * 8, uvs[i].u);
		lmm_wrf32 (buf + uv_off + i * 8 + 4, uvs[i].v);
	}
	for (size_t j = 0; j < nj; j++)
	{
		u8 *np = buf + node_off + j * 16;
		lmm_wr16 (np, (u16)j);
		// child/sibling are relative file-order offsets to the next node
		// sharing our parent (first match), mirroring the reference writer.
		const int p = model->joints[j].parent_idx;
		int sib = -1, child = -1;
		for (size_t k = j + 1; k < nj; k++)
			if (model->joints[k].parent_idx == p)
			{
				sib = (int)k;
				break;
			}
		for (size_t k = 0; k < nj; k++)
			if (model->joints[k].parent_idx == (int)j)
			{
				child = (int)k;
				break;
			}
		lmm_wr16 (np + 2, child >= 0 ? (u16)(child - (int)j) : 0);
		lmm_wr16 (np + 4, sib >= 0 ? (u16)(sib - (int)j) : 0);
		lmm_wr16 (np + 6, 0);
		lmm_wr16 (np + 8, 1); // shapeCount: decoder only uses it for naming
		lmm_wr16 (np + 10, 0);
	}
	for (size_t j = 0; j < nj; j++)
		for (int k = 0; k < 12; k++)
			lmm_wrf32 (buf + matx_off + j * 48 + k * 4, ibinds[j][k]);

	FREE (tex_pix_size);
	FREE (tex_w);
	FREE (tex_h);
	FREE (blob_off);
	FREE (blob_pad);
	FREE (first_child);
	FREE (next_sib);
	FREE (worlds);
	FREE (ibinds);
	FREE (positions);
	FREE (normals);
	FREE (uvs);
	FREE (colours);
	for (size_t k = 0; k < npack; k++)
	{
		FREE (packets[k].blob);
		FREE (packets[k].lod);
	}
	FREE (packets);

	*out = buf;
	*out_size = cur;
	return ERR_OK;
}

enumError EncodeModelToLMMDL (const model_t *model, ccp out_path)
{
	u8 *buf = 0;
	uint size = 0;
	enumError err = EncodeLMMDL (model, &buf, &size);
	if (err || !buf)
	{
		FREE (buf);
		return err ? err : ERR_INVALID_DATA;
	}
	File_t F;
	err = CreateFileOpt (&F, true, out_path, false, out_path);
	if (!err && F.f && fwrite (buf, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing LM MDL failed: %s\n", out_path);
	ResetFile (&F, opt_preserve);
	FREE (buf);
	return err;
}
