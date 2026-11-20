// PowerVR POD reader (PVRTModelPOD, "AB.POD.2.0"), as used by Miitomo.
//
// The file is a flat stream of little-endian (u32 tag, u32 length) blocks.
// Container blocks (scene, mesh, node, material, a mesh's vertex-data
// entries) have length 0 and are closed by their tag | 0x80000000; every
// data block is followed by its own closing tag as well.
//
// Tags used here (PVRTModelPOD.h, EPODFileName):
//   1000 version string                1001 scene
//   2004 #mesh 2005 #node 2006 #mesh node 2009 #frame 2017 fps
//   2012 mesh  2013 node  2015 material (3000 name)
//   node: 5000 index (mesh index for mesh nodes) 5001 name 5002 material
//         5003 parent 5007 position 5008 rotation (quaternion x,y,z,w)
//         5009 scale (7 floats in v2: xyz + stretch quaternion)
//         5011 matrices (column-major 4x4) 5012 animation flags
//         (1 position, 2 rotation, 4 scale, 8 matrix) and 5013..5016
//         per-frame float offsets into 5007/5008/5009/5011.
//   mesh: 6000 #vertex 6001 #face 6004 strip lengths 6005 #strip
//         6014 interleaved vertex data; 6003 faces, 6006 position,
//         6007 normal, 6010 uvw, 6011 colour, 6012 bone index,
//         6013 bone weight are containers of 9000 type, 9001 components,
//         9002 stride, 9003 data (an offset into 6014 when interleaved);
//         6015 bone batch bone lists (node indices), 6016 per-batch bone
//         count, 6017 per-batch first face, 6018 max bones per batch,
//         6019 batch count, 6020 position unpack matrix.
// Layouts verified on the Miitomo body (bodyAll0004.Mdl.pod) and its
// animations (*.Anim.pod).

#include "lib-pvrpod.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct
{
	const uint8_t *p;
	uint32_t len;
} pod_blob_t;

typedef struct
{
	uint32_t type, ncomp, stride;
	pod_blob_t data;
} pod_vdata_t;

typedef struct
{
	uint32_t nvtx, nfaces, nstrips;
	pod_blob_t interleaved, strip_len;
	pod_vdata_t faces, pos, nor, uvw, col, bidx, bwt;
	pod_blob_t batches, batch_cnts, batch_offs;
	uint32_t batch_max, batch_cnt;
	float unpack[16];
	int has_unpack;
} pod_mesh_t;

typedef struct
{
	int32_t idx, mat, parent;
	char name[64];
	uint32_t flags;
	pod_blob_t pos, rot, scale, matrix, pos_idx, rot_idx, scale_idx, matrix_idx;
} pod_node_t;

typedef struct
{
	uint32_t n_mesh_node, n_frame, fps;
	pod_mesh_t *meshes;
	size_t n_meshes;
	pod_node_t *nodes;
	size_t n_nodes;
	char (*mats)[64];
	size_t n_mats;
} pod_scene_t;

static uint32_t le32 (const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static float lef32 (const uint8_t *p)
{
	const uint32_t v = le32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static uint32_t blob_u32 (pod_blob_t b)
{
	return b.len >= 4 ? le32 (b.p) : 0;
}

int IsPowerVRPOD (const uint8_t *data, size_t size)
{
	return data && size >= 0x20 && le32 (data) == 1000 && le32 (data + 4) >= 7
		&& le32 (data + 4) < 64 && !memcmp (data + 8, "AB.POD.", 7);
}

static int pod_is_container (uint32_t tag)
{
	switch (tag)
	{
		case 1001: case 2010: case 2011: case 2012: case 2013: case 2014: case 2015:
		case 6003: case 6006: case 6007: case 6008: case 6009: case 6010: case 6011:
		case 6012: case 6013:
			return 1;
	}
	return 0;
}

static void *pod_grow (void *arr, size_t n, size_t elem)
{
	void *p = realloc (arr, (n + 1) * elem);
	if (p)
		memset ((uint8_t *)p + n * elem, 0, elem);
	return p;
}

static void pod_free_scene (pod_scene_t *s)
{
	free (s->meshes);
	free (s->nodes);
	free (s->mats);
	memset (s, 0, sizeof (*s));
}

static int pod_parse (const uint8_t *d, size_t size, pod_scene_t *s)
{
	memset (s, 0, sizeof (*s));
	if (!IsPowerVRPOD (d, size))
		return 0;
	uint32_t stack[16];
	unsigned sp = 0;
	pod_vdata_t *vd = NULL;
	size_t o = 0;
	while (o + 8 <= size)
	{
		const uint32_t tag = le32 (d + o), len = le32 (d + o + 4);
		o += 8;
		if (tag & 0x80000000u)
		{
			if (sp && stack[sp - 1] == (tag & 0x7fffffffu))
			{
				sp--;
				vd = NULL;
			}
			continue;
		}
		if (pod_is_container (tag) && !len)
		{
			if (sp == 16)
				return 0;
			stack[sp++] = tag;
			const uint32_t parent = sp >= 2 ? stack[sp - 2] : 0;
			if (tag == 2012 && parent == 1001)
			{
				pod_mesh_t *m = pod_grow (s->meshes, s->n_meshes, sizeof (*m));
				if (!m)
					return 0;
				s->meshes = m;
				s->n_meshes++;
			}
			else if (tag == 2013 && parent == 1001)
			{
				pod_node_t *n = pod_grow (s->nodes, s->n_nodes, sizeof (*n));
				if (!n)
					return 0;
				s->nodes = n;
				n[s->n_nodes].parent = -1;
				n[s->n_nodes].idx = -1;
				n[s->n_nodes].mat = -1;
				s->n_nodes++;
			}
			else if (tag == 2015 && parent == 1001)
			{
				char (*mt)[64] = pod_grow (s->mats, s->n_mats, sizeof (*mt));
				if (!mt)
					return 0;
				s->mats = mt;
				s->n_mats++;
			}
			else if (parent == 2012 && s->n_meshes)
			{
				pod_mesh_t *m = &s->meshes[s->n_meshes - 1];
				vd = tag == 6003 ? &m->faces
					: tag == 6006 ? &m->pos
					: tag == 6007 ? &m->nor
					: tag == 6010 ? &m->uvw
					: tag == 6011 ? &m->col
					: tag == 6012 ? &m->bidx
					: tag == 6013 ? &m->bwt
								  : NULL;
			}
			continue;
		}
		if (len > size - o)
			break;
		const pod_blob_t b = { d + o, len };
		o += len;
		const uint32_t top = sp ? stack[sp - 1] : 0;
		if (top == 1001)
		{
			if (tag == 2006)
				s->n_mesh_node = blob_u32 (b);
			else if (tag == 2009)
				s->n_frame = blob_u32 (b);
			else if (tag == 2017)
				s->fps = blob_u32 (b);
		}
		else if (top == 2015 && s->n_mats && tag == 3000)
			snprintf (s->mats[s->n_mats - 1], 64, "%.*s", (int)strnlen ((const char *)b.p, b.len), (const char *)b.p);
		else if (top == 2013 && s->n_nodes)
		{
			pod_node_t *n = &s->nodes[s->n_nodes - 1];
			switch (tag)
			{
				case 5000: n->idx = (int32_t)blob_u32 (b); break;
				case 5001:
					snprintf (n->name, sizeof (n->name), "%.*s", (int)strnlen ((const char *)b.p, b.len),
						(const char *)b.p);
					break;
				case 5002: n->mat = (int32_t)blob_u32 (b); break;
				case 5003: n->parent = (int32_t)blob_u32 (b); break;
				case 5007: n->pos = b; break;
				case 5008: n->rot = b; break;
				case 5009: n->scale = b; break;
				case 5011: n->matrix = b; break;
				case 5012: n->flags = blob_u32 (b); break;
				case 5013: n->pos_idx = b; break;
				case 5014: n->rot_idx = b; break;
				case 5015: n->scale_idx = b; break;
				case 5016: n->matrix_idx = b; break;
			}
		}
		else if (top == 2012 && s->n_meshes)
		{
			pod_mesh_t *m = &s->meshes[s->n_meshes - 1];
			switch (tag)
			{
				case 6000: m->nvtx = blob_u32 (b); break;
				case 6001: m->nfaces = blob_u32 (b); break;
				case 6004: m->strip_len = b; break;
				case 6005: m->nstrips = blob_u32 (b); break;
				case 6014: m->interleaved = b; break;
				case 6015: m->batches = b; break;
				case 6016: m->batch_cnts = b; break;
				case 6017: m->batch_offs = b; break;
				case 6018: m->batch_max = blob_u32 (b); break;
				case 6019: m->batch_cnt = blob_u32 (b); break;
				case 6020:
					if (b.len >= 64)
					{
						for (int k = 0; k < 16; k++)
							m->unpack[k] = lef32 (b.p + k * 4);
						m->has_unpack = 1;
					}
					break;
			}
		}
		else if (vd && top >= 6003 && top <= 6013)
		{
			switch (tag)
			{
				case 9000: vd->type = blob_u32 (b); break;
				case 9001: vd->ncomp = blob_u32 (b); break;
				case 9002: vd->stride = blob_u32 (b); break;
				case 9003: vd->data = b; break;
			}
		}
		// Each data block carries its own closing tag; the loop skips it
		// as an unmatched end tag.
	}
	return 1;
}

//-----------------------------------------------------------------------------
// vertex data

static unsigned pod_type_size (uint32_t type)
{
	switch (type)
	{
		case 1: case 2: case 9: case 17: return 4; // float, int, fixed16.16, uint
		case 3: case 11: case 12: case 16: return 2; // (u)short(norm)
		case 8: return 4; // DEC3N: the whole packed word
		default: return 1; // bytes and 4-byte colours, one byte per component
	}
}

// Component c of vertex i, or `def` when absent.
static float pod_vget (const pod_mesh_t *m, const pod_vdata_t *v, uint32_t i, unsigned c, float def)
{
	if (!v->ncomp || c >= v->ncomp || !v->data.len)
		return def;
	const uint8_t *base;
	size_t avail;
	if (m->interleaved.len && v->data.len == 4)
	{
		const uint32_t off = le32 (v->data.p);
		if (off >= m->interleaved.len)
			return def;
		base = m->interleaved.p + off;
		avail = m->interleaved.len - off;
	}
	else
	{
		base = v->data.p;
		avail = v->data.len;
	}
	const unsigned ts = pod_type_size (v->type);
	const size_t stride = v->stride ? v->stride : (v->type == 8 ? 4 : ts * v->ncomp);
	const size_t at = (size_t)i * stride + (v->type == 8 ? 0 : (size_t)c * ts);
	if (at + ts > avail)
		return def;
	const uint8_t *p = base + at;
	switch (v->type)
	{
		case 1: return lef32 (p);
		case 2: return (float)(int32_t)le32 (p);
		case 17: return (float)le32 (p);
		case 9: return (float)(int32_t)le32 (p) / 65536.0f;
		case 3: return (float)(uint16_t)(p[0] | p[1] << 8);
		case 11: return (float)(int16_t)(p[0] | p[1] << 8);
		case 12: return (float)(int16_t)(p[0] | p[1] << 8) / 32767.0f;
		case 16: return (float)(uint16_t)(p[0] | p[1] << 8) / 65535.0f;
		case 13: return (float)(int8_t)p[0];
		case 14: return (float)(int8_t)p[0] / 127.0f;
		case 15: return (float)p[0] / 255.0f;
		case 4: case 5: case 6: case 18: return (float)p[0] / 255.0f;
		case 8:
		{
			int x = (int)((le32 (p) >> (10 * c)) & 0x3ff);
			if (x & 0x200)
				x -= 0x400;
			return (float)x / 511.0f;
		}
		default: return (float)p[0]; // UnsignedByte, UBYTE4
	}
}

//-----------------------------------------------------------------------------
// transforms

static void quat_to_euler_deg (float qx, float qy, float qz, float qw, vec3_t *out)
{
	// R = Rz * Ry * Rx, the convention joint_t::rotate is exported with.
	const double n = sqrt ((double)qx * qx + (double)qy * qy + (double)qz * qz + (double)qw * qw);
	if (n <= 0.0)
	{
		out->x = out->y = out->z = 0;
		return;
	}
	const double x = qx / n, y = qy / n, z = qz / n, w = qw / n;
	const double r00 = 1 - 2 * (y * y + z * z), r10 = 2 * (x * y + z * w);
	const double r20 = 2 * (x * z - y * w), r21 = 2 * (y * z + x * w), r22 = 1 - 2 * (x * x + y * y);
	const double r01 = 2 * (x * y - z * w), r11 = 1 - 2 * (x * x + z * z);
	double ex, ey, ez;
	const double sy = -r20;
	if (sy >= 0.999999 || sy <= -0.999999)
	{
		ey = sy > 0 ? M_PI / 2 : -M_PI / 2;
		ez = 0;
		ex = atan2 (sy > 0 ? r01 : -r01, r11);
	}
	else
	{
		ey = asin (sy);
		ex = atan2 (r21, r22);
		ez = atan2 (r10, r00);
	}
	out->x = (float)(ex * 180.0 / M_PI);
	out->y = (float)(ey * 180.0 / M_PI);
	out->z = (float)(ez * 180.0 / M_PI);
}

// Column-major 4x4 -> translation, rotation quaternion (x,y,z,w), scale.
static void mat_decompose (const float *m, float t[3], float q[4], float s[3])
{
	t[0] = m[12];
	t[1] = m[13];
	t[2] = m[14];
	double r[3][3];
	for (int c = 0; c < 3; c++)
	{
		const double l = sqrt ((double)m[c * 4] * m[c * 4] + (double)m[c * 4 + 1] * m[c * 4 + 1]
			+ (double)m[c * 4 + 2] * m[c * 4 + 2]);
		s[c] = (float)l;
		for (int k = 0; k < 3; k++)
			r[k][c] = l > 0 ? m[c * 4 + k] / l : 0;
	}
	const double det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1])
		- r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0])
		+ r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
	if (det < 0)
	{
		s[0] = -s[0];
		for (int k = 0; k < 3; k++)
			r[k][0] = -r[k][0];
	}
	const double tr = r[0][0] + r[1][1] + r[2][2];
	double x, y, z, w;
	if (tr > 0)
	{
		const double k = sqrt (tr + 1.0) * 2;
		w = 0.25 * k;
		x = (r[2][1] - r[1][2]) / k;
		y = (r[0][2] - r[2][0]) / k;
		z = (r[1][0] - r[0][1]) / k;
	}
	else if (r[0][0] > r[1][1] && r[0][0] > r[2][2])
	{
		const double k = sqrt (1.0 + r[0][0] - r[1][1] - r[2][2]) * 2;
		w = (r[2][1] - r[1][2]) / k;
		x = 0.25 * k;
		y = (r[0][1] + r[1][0]) / k;
		z = (r[0][2] + r[2][0]) / k;
	}
	else if (r[1][1] > r[2][2])
	{
		const double k = sqrt (1.0 + r[1][1] - r[0][0] - r[2][2]) * 2;
		w = (r[0][2] - r[2][0]) / k;
		x = (r[0][1] + r[1][0]) / k;
		y = 0.25 * k;
		z = (r[1][2] + r[2][1]) / k;
	}
	else
	{
		const double k = sqrt (1.0 + r[2][2] - r[0][0] - r[1][1]) * 2;
		w = (r[1][0] - r[0][1]) / k;
		x = (r[0][2] + r[2][0]) / k;
		y = (r[1][2] + r[2][1]) / k;
		z = 0.25 * k;
	}
	q[0] = (float)x;
	q[1] = (float)y;
	q[2] = (float)z;
	q[3] = (float)w;
}

// Reads `count` floats of one frame from a node track: per-frame offsets
// from `idx` when present, else frame * count (or 0 for a static track).
static int pod_track (pod_blob_t data, pod_blob_t idx, uint32_t frame, unsigned count, float *out)
{
	size_t off = 0;
	if (idx.len >= (frame + 1) * 4)
		off = (size_t)le32 (idx.p + (size_t)frame * 4) * 4;
	else if (data.len >= ((size_t)frame + 1) * count * 4)
		off = (size_t)frame * count * 4;
	if (off + (size_t)count * 4 > data.len)
		return 0;
	for (unsigned k = 0; k < count; k++)
		out[k] = lef32 (data.p + off + k * 4);
	return 1;
}

// Local transform of a node at a frame as T, R (quaternion) and S.
static void pod_node_trs (const pod_node_t *n, uint32_t frame, float t[3], float q[4], float s[3])
{
	t[0] = t[1] = t[2] = 0;
	q[0] = q[1] = q[2] = 0;
	q[3] = 1;
	s[0] = s[1] = s[2] = 1;
	if (n->matrix.len >= 64 && (n->flags & 8 || !(n->pos.len || n->rot.len)))
	{
		float m[16];
		if (pod_track (n->matrix, n->matrix_idx, n->flags & 8 ? frame : 0, 16, m))
		{
			mat_decompose (m, t, q, s);
			return;
		}
	}
	pod_track (n->pos, n->pos_idx, n->flags & 1 ? frame : 0, 3, t);
	pod_track (n->rot, n->rot_idx, n->flags & 2 ? frame : 0, 4, q);
	// v2 stores 7 floats per scale key (xyz + stretch rotation), v1 three.
	float sv[7];
	const unsigned sc = n->scale.len % 28 == 0 && n->scale.len ? 7 : 3;
	if (pod_track (n->scale, n->scale_idx, n->flags & 4 ? frame : 0, sc, sv))
		memcpy (s, sv, sizeof (float) * 3);
}

static void mat_from_trs (const float t[3], const float q[4], const float s[3], float m[16])
{
	const float x = q[0], y = q[1], z = q[2], w = q[3];
	const float r[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
		2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 2 * (x * z + y * w),
		2 * (y * z - x * w), 1 - 2 * (x * x + y * y) };
	for (int c = 0; c < 3; c++)
	{
		for (int k = 0; k < 3; k++)
			m[c * 4 + k] = r[c * 3 + k] * s[c];
		m[c * 4 + 3] = 0;
	}
	m[12] = t[0];
	m[13] = t[1];
	m[14] = t[2];
	m[15] = 1;
}

static void mat_mul (const float a[16], const float b[16], float o[16])
{
	float r[16];
	for (int c = 0; c < 4; c++)
		for (int k = 0; k < 4; k++)
			r[c * 4 + k] = a[k] * b[c * 4] + a[4 + k] * b[c * 4 + 1] + a[8 + k] * b[c * 4 + 2]
				+ a[12 + k] * b[c * 4 + 3];
	memcpy (o, r, sizeof (r));
}

static void pod_node_world (const pod_scene_t *s, size_t i, float m[16], int depth)
{
	float t[3], q[4], sc[3];
	pod_node_trs (&s->nodes[i], 0, t, q, sc);
	mat_from_trs (t, q, sc, m);
	const int32_t p = s->nodes[i].parent;
	if (p >= 0 && (size_t)p < s->n_nodes && depth < 64)
	{
		float pm[16];
		pod_node_world (s, (size_t)p, pm, depth + 1);
		mat_mul (pm, m, m);
	}
}

//-----------------------------------------------------------------------------
// model

static int pod_influence_add (model_t *model, size_t *cap, const int *bones, const float *w, int n)
{
	for (size_t i = 0; i < model->num_node_influences; i++)
	{
		const node_influence_t *e = &model->node_influences[i];
		if (e->num_weights != (size_t)n)
			continue;
		int same = 1;
		for (int k = 0; k < n && same; k++)
			same = e->weights[k].bone_idx == bones[k] && fabsf (e->weights[k].weight - w[k]) < 1e-6f;
		if (same)
			return (int)i;
	}
	if (model->num_node_influences == *cap)
	{
		const size_t nc = *cap ? *cap * 2 : 64;
		node_influence_t *ni = realloc (model->node_influences, nc * sizeof (*ni));
		if (!ni)
			return -1;
		model->node_influences = ni;
		*cap = nc;
	}
	influence_t *iw = calloc ((size_t)n, sizeof (*iw));
	if (!iw)
		return -1;
	for (int k = 0; k < n; k++)
	{
		iw[k].bone_idx = bones[k];
		iw[k].weight = w[k];
	}
	node_influence_t *e = &model->node_influences[model->num_node_influences];
	e->weights = iw;
	e->num_weights = (size_t)n;
	return (int)model->num_node_influences++;
}

// Triangle-list vertex indices for a mesh (strips expanded).
static uint32_t *pod_indices (const pod_mesh_t *m, size_t *count)
{
	*count = 0;
	const pod_vdata_t *f = &m->faces;
	const unsigned ts = f->type == 17 ? 4 : 2;
	const size_t navail = f->data.len / ts;
	if (!navail)
		return NULL;
	uint32_t *raw = malloc (navail * sizeof (uint32_t));
	if (!raw)
		return NULL;
	for (size_t i = 0; i < navail; i++)
		raw[i] = ts == 4 ? le32 (f->data.p + i * 4) : (uint32_t)(f->data.p[i * 2] | f->data.p[i * 2 + 1] << 8);
	if (!m->nstrips)
	{
		size_t n = (size_t)m->nfaces * 3;
		if (n > navail)
			n = navail - navail % 3;
		*count = n;
		return raw;
	}
	uint32_t *out = malloc ((size_t)m->nfaces * 3 * sizeof (uint32_t) + 1);
	if (!out)
	{
		free (raw);
		return NULL;
	}
	size_t at = 0, n = 0;
	for (uint32_t st = 0; st < m->nstrips && (st + 1) * 4 <= m->strip_len.len; st++)
	{
		const uint32_t tris = le32 (m->strip_len.p + st * 4);
		for (uint32_t t = 0; t < tris && at + t + 2 < navail && n + 3 <= (size_t)m->nfaces * 3; t++)
		{
			const uint32_t a = raw[at + t], b = raw[at + t + 1], c = raw[at + t + 2];
			out[n++] = a;
			out[n++] = t & 1 ? c : b;
			out[n++] = t & 1 ? b : c;
		}
		at += tris + 2;
	}
	free (raw);
	*count = n;
	return out;
}

model_t *ParsePowerVRPOD (const uint8_t *data, size_t size)
{
	pod_scene_t s;
	if (!pod_parse (data, size, &s) || !s.n_meshes || !s.n_mesh_node || s.n_mesh_node > s.n_nodes)
	{
		pod_free_scene (&s);
		return NULL;
	}
	model_t *model = calloc (1, sizeof (model_t));
	if (!model)
	{
		pod_free_scene (&s);
		return NULL;
	}

	// Joints: every node that is not a mesh instance (mesh nodes come first).
	const size_t nmn = s.n_mesh_node;
	model->num_joints = s.n_nodes - nmn;
	model->joints = model->num_joints ? calloc (model->num_joints, sizeof (joint_t)) : NULL;
	for (size_t j = 0; j < model->num_joints; j++)
	{
		const pod_node_t *n = &s.nodes[nmn + j];
		joint_t *jt = &model->joints[j];
		snprintf (jt->name, sizeof (jt->name), "%s", n->name);
		jt->parent_idx = n->parent >= (int32_t)nmn && (size_t)n->parent < s.n_nodes ? n->parent - (int)nmn : -1;
		float t[3], q[4], sc[3];
		pod_node_trs (n, 0, t, q, sc);
		jt->translate.x = t[0];
		jt->translate.y = t[1];
		jt->translate.z = t[2];
		quat_to_euler_deg (q[0], q[1], q[2], q[3], &jt->rotate);
		jt->scale.x = sc[0];
		jt->scale.y = sc[1];
		jt->scale.z = sc[2];
	}

	model->num_materials = s.n_mats;
	model->materials = s.n_mats ? calloc (s.n_mats, sizeof (material_t)) : NULL;
	for (size_t i = 0; i < s.n_mats && model->materials; i++)
		snprintf (model->materials[i].name, sizeof (model->materials[i].name), "%s", s.mats[i]);

	model->meshes = calloc (nmn, sizeof (mesh_t));
	size_t inf_cap = 0;
	for (size_t mn = 0; mn < nmn && model->meshes; mn++)
	{
		const pod_node_t *node = &s.nodes[mn];
		if (node->idx < 0 || (size_t)node->idx >= s.n_meshes)
			continue;
		const pod_mesh_t *pm = &s.meshes[node->idx];
		size_t nidx = 0;
		uint32_t *idx = pod_indices (pm, &nidx);
		const uint32_t nv = pm->nvtx;
		if (!idx || !nidx || !nv || !pm->pos.ncomp)
		{
			free (idx);
			continue;
		}
		mesh_t *mesh = &model->meshes[model->num_meshes];
		snprintf (mesh->name, sizeof (mesh->name), "%s",
			node->mat >= 0 && (size_t)node->mat < s.n_mats ? s.mats[node->mat] : node->name);
		mesh->material_idx = node->mat >= 0 && (size_t)node->mat < s.n_mats ? node->mat : -1;
		mesh->positions = calloc (nv, sizeof (vec3_t));
		mesh->num_positions = nv;
		const int has_nrm = pm->nor.ncomp >= 3, has_uv = pm->uvw.ncomp >= 2;
		mesh->normals = has_nrm ? calloc (nv, sizeof (vec3_t)) : NULL;
		mesh->num_normals = has_nrm ? nv : 0;
		mesh->texcoords = has_uv ? calloc (nv, sizeof (vec2_t)) : NULL;
		mesh->num_texcoords = has_uv ? nv : 0;
		mesh->vertices = calloc (nidx, sizeof (vertex_t));
		mesh->num_vertices = nidx;
		const int skinned = pm->bidx.ncomp && pm->batch_cnt && model->num_joints;
		mesh->position_node = skinned ? malloc (nv * sizeof (int)) : NULL;
		if (!mesh->positions || !mesh->vertices || (has_nrm && !mesh->normals)
			|| (has_uv && !mesh->texcoords) || (skinned && !mesh->position_node))
		{
			free (idx);
			break;
		}

		// Unskinned meshes sit under their mesh node; skinned ones are
		// stored in model space (bind pose = the nodes' first frame).
		float world[16];
		pod_node_world (&s, mn, world, 0);
		const int use_unpack = pm->has_unpack;
		for (uint32_t v = 0; v < nv; v++)
		{
			float p[3] = { pod_vget (pm, &pm->pos, v, 0, 0), pod_vget (pm, &pm->pos, v, 1, 0),
				pod_vget (pm, &pm->pos, v, 2, 0) };
			if (use_unpack)
			{
				const float *u = pm->unpack;
				const float q[3] = { u[0] * p[0] + u[4] * p[1] + u[8] * p[2] + u[12],
					u[1] * p[0] + u[5] * p[1] + u[9] * p[2] + u[13],
					u[2] * p[0] + u[6] * p[1] + u[10] * p[2] + u[14] };
				memcpy (p, q, sizeof (p));
			}
			float nrm[3] = { pod_vget (pm, &pm->nor, v, 0, 0), pod_vget (pm, &pm->nor, v, 1, 0),
				pod_vget (pm, &pm->nor, v, 2, 1) };
			if (!skinned)
			{
				const float q[3] = { world[0] * p[0] + world[4] * p[1] + world[8] * p[2] + world[12],
					world[1] * p[0] + world[5] * p[1] + world[9] * p[2] + world[13],
					world[2] * p[0] + world[6] * p[1] + world[10] * p[2] + world[14] };
				const float r[3] = { world[0] * nrm[0] + world[4] * nrm[1] + world[8] * nrm[2],
					world[1] * nrm[0] + world[5] * nrm[1] + world[9] * nrm[2],
					world[2] * nrm[0] + world[6] * nrm[1] + world[10] * nrm[2] };
				memcpy (p, q, sizeof (p));
				memcpy (nrm, r, sizeof (nrm));
			}
			mesh->positions[v].x = p[0];
			mesh->positions[v].y = p[1];
			mesh->positions[v].z = p[2];
			if (has_nrm)
			{
				mesh->normals[v].x = nrm[0];
				mesh->normals[v].y = nrm[1];
				mesh->normals[v].z = nrm[2];
			}
			if (has_uv)
			{
				mesh->texcoords[v].u = pod_vget (pm, &pm->uvw, v, 0, 0);
				mesh->texcoords[v].v = pod_vget (pm, &pm->uvw, v, 1, 0);
			}
			if (skinned)
				mesh->position_node[v] = -1;
		}
		for (size_t k = 0; k < nidx; k++)
		{
			const int vi = idx[k] < nv ? (int)idx[k] : 0;
			vertex_t *vx = &mesh->vertices[k];
			memset (vx, 0xff, sizeof (*vx)); // every index -1
			vx->position_idx = vi;
			vx->normal_idx = has_nrm ? vi : -1;
			vx->texcoord_idx = has_uv ? vi : -1;
		}

		// Skinning: a vertex's bone indices select from the bone list of
		// the batch whose face range uses it; list entries are node indices.
		if (skinned)
		{
			for (uint32_t b = 0; b < pm->batch_cnt; b++)
			{
				const uint32_t f0 = (b + 1) * 4 <= pm->batch_offs.len ? le32 (pm->batch_offs.p + b * 4) : 0;
				const uint32_t f1 = b + 1 < pm->batch_cnt && (b + 2) * 4 <= pm->batch_offs.len
					? le32 (pm->batch_offs.p + (b + 1) * 4)
					: (uint32_t)(nidx / 3);
				const uint32_t nb = (b + 1) * 4 <= pm->batch_cnts.len ? le32 (pm->batch_cnts.p + b * 4) : pm->batch_max;
				for (size_t k = (size_t)f0 * 3; k < (size_t)f1 * 3 && k < nidx; k++)
				{
					const uint32_t v = idx[k];
					if (v >= nv || mesh->position_node[v] >= 0)
						continue;
					int bones[4];
					float w[4];
					int cnt = 0;
					for (unsigned c = 0; c < pm->bidx.ncomp && c < 4; c++)
					{
						const float wt = pm->bwt.ncomp ? pod_vget (pm, &pm->bwt, v, c, 0) : (c ? 0.0f : 1.0f);
						const int li = (int)pod_vget (pm, &pm->bidx, v, c, 0);
						if (wt <= 0.0f || li < 0 || (uint32_t)li >= nb)
							continue;
						const size_t at = ((size_t)b * pm->batch_max + (size_t)li) * 4;
						if (at + 4 > pm->batches.len)
							continue;
						const int32_t nodei = (int32_t)le32 (pm->batches.p + at);
						if (nodei < (int32_t)nmn || (size_t)nodei >= s.n_nodes)
							continue;
						bones[cnt] = nodei - (int)nmn;
						w[cnt] = wt;
						cnt++;
					}
					float sum = 0;
					for (int c = 0; c < cnt; c++)
						sum += w[c];
					if (!cnt || sum <= 0)
						continue;
					for (int c = 0; c < cnt; c++)
						w[c] /= sum;
					mesh->position_node[v] = pod_influence_add (model, &inf_cap, bones, w, cnt);
				}
			}
		}
		free (idx);
		model->num_meshes++;
	}
	pod_free_scene (&s);

	if (!model->num_meshes)
	{
		FreeModel (model);
		return NULL;
	}
	ComputeModelTRSBinds (model);
	return model;
}

//-----------------------------------------------------------------------------
// animation

static void pod_add_channel (model_animation_t *a, int node, model_anim_path_t path, float *times,
	float *values, size_t count, size_t comps)
{
	model_anim_channel_t *ch = realloc (a->channels, (a->num_channels + 1) * sizeof (*ch));
	if (!ch)
	{
		free (times);
		free (values);
		return;
	}
	a->channels = ch;
	ch += a->num_channels++;
	ch->node_idx = node;
	ch->path = path;
	ch->times = times;
	ch->values = values;
	ch->count = count;
	ch->components = comps;
}

int ImportPowerVRPODAnim (model_t *model, const uint8_t *data, size_t size, const char *clip_name)
{
	pod_scene_t s;
	if (!model || !model->num_joints || !pod_parse (data, size, &s))
		return 0;
	const uint32_t nframes = s.n_frame ? s.n_frame : 1;
	const float fps = s.fps ? (float)s.fps : 30.0f;
	model_animation_t anim;
	memset (&anim, 0, sizeof (anim));
	snprintf (anim.name, sizeof (anim.name), "%s", clip_name && *clip_name ? clip_name : "anim");

	for (size_t i = 0; i < s.n_nodes; i++)
	{
		// Animated nodes are sampled per frame; the others hold one static
		// transform (a pose file has nothing else), exported as one key.
		const pod_node_t *n = &s.nodes[i];
		if (!n->name[0] || !(n->matrix.len || n->pos.len || n->rot.len))
			continue;
		const uint32_t nkeys = n->flags & 0xf ? nframes : 1;
		int j = -1;
		for (size_t k = 0; k < model->num_joints; k++)
			if (!strcmp (model->joints[k].name, n->name))
			{
				j = (int)k;
				break;
			}
		if (j < 0)
			continue;
		float *tt = malloc (nkeys * sizeof (float)), *rt = malloc (nkeys * sizeof (float));
		float *st = malloc (nkeys * sizeof (float));
		float *tv = malloc (nkeys * 3 * sizeof (float)), *rv = malloc (nkeys * 4 * sizeof (float));
		float *sv = malloc (nkeys * 3 * sizeof (float));
		if (!tt || !rt || !st || !tv || !rv || !sv)
		{
			free (tt), free (rt), free (st), free (tv), free (rv), free (sv);
			continue;
		}
		for (uint32_t f = 0; f < nkeys; f++)
		{
			tt[f] = rt[f] = st[f] = (float)f / fps;
			pod_node_trs (n, f, tv + f * 3, rv + f * 4, sv + f * 3);
			// Keep consecutive quaternions in one hemisphere for interpolation.
			if (f && rv[f * 4] * rv[f * 4 - 4] + rv[f * 4 + 1] * rv[f * 4 - 3]
					+ rv[f * 4 + 2] * rv[f * 4 - 2] + rv[f * 4 + 3] * rv[f * 4 - 1]
				< 0)
				for (int c = 0; c < 4; c++)
					rv[f * 4 + c] = -rv[f * 4 + c];
		}
		pod_add_channel (&anim, j, MODEL_ANIM_TRANSLATION, tt, tv, nkeys, 3);
		pod_add_channel (&anim, j, MODEL_ANIM_ROTATION, rt, rv, nkeys, 4);
		pod_add_channel (&anim, j, MODEL_ANIM_SCALE, st, sv, nkeys, 3);
	}
	pod_free_scene (&s);
	if (!anim.num_channels)
	{
		free (anim.channels);
		return 0;
	}
	model_animation_t *arr = realloc (model->animations, (model->num_animations + 1) * sizeof (*arr));
	if (!arr)
	{
		for (size_t c = 0; c < anim.num_channels; c++)
		{
			free (anim.channels[c].times);
			free (anim.channels[c].values);
		}
		free (anim.channels);
		return 0;
	}
	model->animations = arr;
	model->animations[model->num_animations++] = anim;
	return 1;
}
