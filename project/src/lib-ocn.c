
/***************************************************************************
 *                                                                         *
 *   Paon "OCN." skinned model container, see lib-ocn.h                    *
 *                                                                         *
 ***************************************************************************/

#include "lib-ocn.h"
#include "lib-image.h"
#include "lib-model-glb.h"

#include <math.h>
#include <string.h>

#define OCN_HDR 0x20 // every chunk's payload starts after this many bytes
#define OCN_MAX_BONES 256
#define OCN_COPY_SLACK 64 // zero padding for the image decoders

typedef struct ocn_bone_t
{
	char name[64];
	int parent;
	float q[4], s[3], t[3];
	float w[12]; // world matrix, 3x4 row major
} ocn_bone_t;

typedef struct ocn_ent_t
{
	int bone;
	float pos[3], nrm[3], weight;
} ocn_ent_t;

typedef struct ocn_rec_t
{
	uint n;
	ocn_ent_t e[4];
	float pos[3], nrm[3]; // blended, model space
} ocn_rec_t;

///////////////////////////////////////////////////////////////////////////////

// Tags are stored byte-reversed.
static bool ocn_tag (const u8 *p, ccp tag)
{
	return p[0] == (u8)tag[3] && p[1] == (u8)tag[2] && p[2] == (u8)tag[1] && p[3] == (u8)tag[0];
}

static inline float bef32 (const u8 *p)
{
	const u32 v = be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static inline uint ocn_end (const u8 *p)
{
	return be32 (p + 4) + 8;
}

// Offset of the chunk after 'o', or 0 when it would leave [0,limit).
static uint ocn_next (const u8 *data, uint limit, uint o)
{
	if (o + OCN_HDR > limit)
		return 0;
	const uint n = o + ocn_end (data + o);
	return n > o && n <= limit ? n : 0;
}

bool IsOCN (const u8 *data, uint size)
{
	return data && size >= 0x80 && !memcmp (data, "OCN.", 4) && be32 (data + 4) == 0x18;
}

///////////////////////////////////////////////////////////////////////////////

static void ocn_quat_to_rot (const float *q, float r[9])
{
	double x = q[0], y = q[1], z = q[2], w = q[3];
	double n = sqrt (x * x + y * y + z * z + w * w);
	if (n < 1e-12)
		n = 1.0, w = 1.0;
	x /= n, y /= n, z /= n, w /= n;
	r[0] = (float)(1 - 2 * (y * y + z * z));
	r[1] = (float)(2 * (x * y - z * w));
	r[2] = (float)(2 * (x * z + y * w));
	r[3] = (float)(2 * (x * y + z * w));
	r[4] = (float)(1 - 2 * (x * x + z * z));
	r[5] = (float)(2 * (y * z - x * w));
	r[6] = (float)(2 * (x * z - y * w));
	r[7] = (float)(2 * (y * z + x * w));
	r[8] = (float)(1 - 2 * (x * x + y * y));
}

static void ocn_bone_world (ocn_bone_t *b, const ocn_bone_t *bones)
{
	float r[9];
	ocn_quat_to_rot (b->q, r);
	float l[12];
	for (uint i = 0; i < 3; i++)
	{
		l[i * 4 + 0] = r[i * 3 + 0] * b->s[0];
		l[i * 4 + 1] = r[i * 3 + 1] * b->s[1];
		l[i * 4 + 2] = r[i * 3 + 2] * b->s[2];
		l[i * 4 + 3] = b->t[i];
	}
	if (b->parent < 0)
	{
		memcpy (b->w, l, sizeof (l));
		return;
	}
	const float *p = bones[b->parent].w;
	for (uint i = 0; i < 3; i++)
	{
		for (uint j = 0; j < 3; j++)
			b->w[i * 4 + j] = p[i * 4] * l[j] + p[i * 4 + 1] * l[4 + j] + p[i * 4 + 2] * l[8 + j];
		b->w[i * 4 + 3] = p[i * 4] * l[3] + p[i * 4 + 1] * l[7] + p[i * 4 + 2] * l[11] + p[i * 4 + 3];
	}
}

// joint_t stores XYZ euler degrees composed as Rz * Ry * Rx (see lib-model-glb.c).
static vec3_t ocn_euler_deg (const float *q)
{
	float r[9];
	ocn_quat_to_rot (q, r);
	double sy = -r[6];
	sy = sy > 1.0 ? 1.0 : sy < -1.0 ? -1.0 : sy;
	const double ry = asin (sy);
	double rx, rz;
	if (fabs (sy) < 0.999999)
	{
		rx = atan2 (r[7], r[8]);
		rz = atan2 (r[3], r[0]);
	}
	else
	{
		rx = atan2 (-r[5], r[4]);
		rz = 0.0;
	}
	const double k = 180.0 / M_PI;
	return (vec3_t) { (float)(rx * k), (float)(ry * k), (float)(rz * k) };
}

///////////////////////////////////////////////////////////////////////////////

static void ocn_save_texture (
	const u8 *tex, uint tex_size, uint fmt, uint w, uint h, ccp dir, ccp base, uint idx)
{
	if (!w || !h || w > 2048 || h > 2048)
		return;

	Image_t img;
	InitializeIMG (&img);
	img.data = CALLOC (1, tex_size + OCN_COPY_SLACK);
	memcpy (img.data, tex, tex_size);
	img.data_alloced = true;
	img.data_size = tex_size;
	img.width = w;
	img.height = h;
	img.iform = img.info_iform = fmt;
	img.info_fform = FF_PNG;
	img.info_n_image = 1;
	img.endian = &be_func;
	CalcImageGeometry (img.iform, img.width, img.height, &img.xwidth, &img.xheight, 0, 0, 0);

	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%s.tex%03u.png", dir, base, idx);
	if (!ConvertIMG (&img, false, 0, IMG_X_RGB, PAL_INVALID))
		SavePNG (&img, false, 0, path, 0, 0, true, 0);
	ResetIMG (&img);
}

///////////////////////////////////////////////////////////////////////////////

// Appends the triangles of one GX display list ('dl', n bytes) to 'tri'
// as triples of (pos,nrm,uv) index u16 triples.
typedef struct ocn_tri_t
{
	u16 v[3][3];
} ocn_tri_t;

static void ocn_add_tri (ocn_tri_t **tri, uint *n, uint *cap, const u8 *a, const u8 *b, const u8 *c)
{
	if (be16 (a) == be16 (b) || be16 (b) == be16 (c) || be16 (a) == be16 (c))
		return; // degenerate strip stitch
	if (*n == *cap)
	{
		*cap = *cap ? *cap * 2 : 256;
		*tri = REALLOC (*tri, *cap * sizeof (**tri));
	}
	ocn_tri_t *t = *tri + (*n)++;
	const u8 *src[3] = { a, b, c };
	for (uint i = 0; i < 3; i++)
		for (uint k = 0; k < 3; k++)
			t->v[i][k] = be16 (src[i] + 2 * k);
}

static uint ocn_display_list (const u8 *dl, uint size, ocn_tri_t **tri, uint *n, uint *cap)
{
	uint o = 0;
	while (o + 3 <= size && dl[o])
	{
		const uint op = dl[o], cnt = be16 (dl + o + 1);
		o += 3;
		if (o + 6 * (u64)cnt > size)
			break;
		const u8 *v = dl + o;
		o += 6 * cnt;
		switch (op & 0xf8)
		{
		case 0x98: // triangle strip
			for (uint i = 0; i + 2 < cnt; i++)
			{
				if (i & 1)
					ocn_add_tri (tri, n, cap, v + 6 * (i + 1), v + 6 * i, v + 6 * (i + 2));
				else
					ocn_add_tri (tri, n, cap, v + 6 * i, v + 6 * (i + 1), v + 6 * (i + 2));
			}
			break;
		case 0x90: // triangles
			for (uint i = 0; i + 2 < cnt; i += 3)
				ocn_add_tri (tri, n, cap, v + 6 * i, v + 6 * (i + 1), v + 6 * (i + 2));
			break;
		case 0xa0: // triangle fan
			for (uint i = 1; i + 1 < cnt; i++)
				ocn_add_tri (tri, n, cap, v, v + 6 * i, v + 6 * (i + 1));
			break;
		case 0x80: // quads
			for (uint i = 0; i + 3 < cnt; i += 4)
			{
				ocn_add_tri (tri, n, cap, v + 6 * i, v + 6 * (i + 1), v + 6 * (i + 2));
				ocn_add_tri (tri, n, cap, v + 6 * i, v + 6 * (i + 2), v + 6 * (i + 3));
			}
			break;
		default:
			return *n;
		}
	}
	return *n;
}

///////////////////////////////////////////////////////////////////////////////

// Decode the WEIG pool and blend each vertex into model space. Two layouts
// exist: the compact one (s16 positions, 1/512, u16 weights, 0x400 = 1) and a
// float one (f32 positions and weights) used by the *_4000 detail models.
static bool ocn_weig_layout (const u8 *w, uint size, uint count, uint *ret_k, bool *ret_f32)
{
	for (uint pass = 0; pass < 2; pass++)
	{
		const bool f32 = pass == 1;
		for (uint k = 1; k <= 4; k++)
		{
			const uint stride = 4 + (f32 ? 24 : 16) * k;
			if ((count * (stride + 4 * k) + 31) / 32 * 32 != size)
				continue;
			// first vertex: the weights must add up to one
			double sum = 0;
			for (uint i = 0; i < k; i++)
				sum += f32 ? bef32 (w + 4 + 16 * i + 12) : be16 (w + 4 + 8 * i + 6) / 1024.0;
			if (fabs (sum - 1.0) < 0.02)
			{
				*ret_k = k;
				*ret_f32 = f32;
				return true;
			}
		}
	}
	return false;
}

static ocn_rec_t *ocn_read_weig (
	const ocn_bone_t *bones, uint n_bones, const u8 *data, uint chunk, uint *ret_count)
{
	*ret_count = 0;
	const uint size = ocn_end (data + chunk) - OCN_HDR;
	const u8 *w = data + chunk + OCN_HDR;
	const uint count = be16 (data + chunk + 0x0c);

	uint k = 0;
	bool f32 = false;
	if (!count || !ocn_weig_layout (w, size, count, &k, &f32))
		return 0;

	const uint ent_size = f32 ? 16 : 8;
	const uint stride = 4 + (ent_size + 8) * k;
	ocn_rec_t *rec = CALLOC (count, sizeof (*rec));
	for (uint v = 0; v < count; v++)
	{
		const u8 *r = w + v * stride;
		const u8 *nr = r + 4 + ent_size * k;
		ocn_rec_t *o = rec + v;
		float total = 0;
		for (uint i = 0; i < k; i++)
		{
			const u8 *pe = r + 4 + ent_size * i;
			const float wt = f32 ? bef32 (pe + 12) : (float)be16 (pe + 6);
			if (!(wt > 0) || r[i] >= n_bones)
				continue;
			ocn_ent_t *e = o->e + o->n++;
			e->bone = r[i];
			e->weight = wt;
			total += wt;
			for (uint j = 0; j < 3; j++)
			{
				e->pos[j] = f32 ? bef32 (pe + 4 * j) : (float)(s16)be16 (pe + 2 * j) / 512.0f;
				e->nrm[j] = (float)(s16)be16 (nr + 8 * i + 2 * j) / 16384.0f;
			}
		}
		if (total <= 0)
			continue;
		for (uint i = 0; i < o->n; i++)
		{
			ocn_ent_t *e = o->e + i;
			e->weight /= total;
			const float *m = bones[e->bone].w;
			for (uint j = 0; j < 3; j++)
			{
				o->pos[j] += e->weight
					* (m[j * 4] * e->pos[0] + m[j * 4 + 1] * e->pos[1] + m[j * 4 + 2] * e->pos[2]
						+ m[j * 4 + 3]);
				o->nrm[j] += e->weight
					* (m[j * 4] * e->nrm[0] + m[j * 4 + 1] * e->nrm[1] + m[j * 4 + 2] * e->nrm[2]);
			}
		}
		const float l = sqrtf (o->nrm[0] * o->nrm[0] + o->nrm[1] * o->nrm[1] + o->nrm[2] * o->nrm[2]);
		if (l > 1e-6f)
			for (uint j = 0; j < 3; j++)
				o->nrm[j] /= l;
	}
	*ret_count = count;
	return rec;
}

///////////////////////////////////////////////////////////////////////////////

static void ocn_free_model (model_t *m)
{
	for (size_t i = 0; i < m->num_meshes; i++)
	{
		mesh_t *me = m->meshes + i;
		FREE (me->positions);
		FREE (me->normals);
		FREE (me->texcoords);
		FREE (me->position_node);
		FREE (me->vertices);
	}
	FREE (m->meshes);
	for (size_t i = 0; i < m->num_node_influences; i++)
		FREE (m->node_influences[i].weights);
	FREE (m->node_influences);
	FREE (m->joints);
	FREE (m->materials);
}

int ExportOCNModel (const u8 *data, uint size, ccp out_glb_file, ccp tex_base)
{
	if (!IsOCN (data, size) || !out_glb_file)
		return -1;

	char dir[PATH_MAX];
	StringCopyS (dir, sizeof (dir), out_glb_file);
	char *slash = strrchr (dir, '/');
	if (slash)
		*slash = 0;
	else
		StringCopyS (dir, sizeof (dir), ".");
	if (!tex_base || !*tex_base)
		tex_base = "ocn";

	// top level: TEXS and MDLS
	uint texs = 0, mdls = 0;
	for (uint o = OCN_HDR, n; o && o < size; o = n)
	{
		n = ocn_next (data, size, o);
		if (ocn_tag (data + o, "TEXS"))
			texs = o;
		else if (ocn_tag (data + o, "MDLS"))
			mdls = o;
	}
	if (!mdls || mdls + 2 * OCN_HDR > size || !ocn_tag (data + mdls + OCN_HDR, "MDEL"))
		return 0;

	if (texs)
	{
		CreatePath (dir, true);
		const uint end = texs + ocn_end (data + texs) < size ? texs + ocn_end (data + texs) : size;
		for (uint c = texs + OCN_HDR, n; c && c < end; c = n)
		{
			n = ocn_next (data, end, c);
			if (ocn_tag (data + c, "ENDE"))
				break;
			if (!ocn_tag (data + c, "TEXT") || !n || n < c + 0x40)
				continue;
			ocn_save_texture (data + c + 0x40, n - c - 0x40, be16 (data + c + 0x20),
				be16 (data + c + 0x22), be16 (data + c + 0x24), dir, tex_base,
				be16 (data + c + 0x0c));
		}
	}

	const uint mdel = mdls + OCN_HDR;
	const uint mdel_end = mdel + ocn_end (data + mdel) < size ? mdel + ocn_end (data + mdel) : size;

	// pass 1: materials, bones
	model_t model;
	memset (&model, 0, sizeof (model));
	ocn_bone_t *bones = CALLOC (OCN_MAX_BONES, sizeof (*bones));
	uint n_bones = 0, n_mates = 0;
	int *mate_tex = CALLOC (1024, sizeof (*mate_tex));
	for (uint c = mdel + OCN_HDR, n; c && c < mdel_end; c = n)
	{
		n = ocn_next (data, mdel_end, c);
		if (!n)
			break;
		if (ocn_tag (data + c, "MATE") && n_mates < 1024)
			mate_tex[n_mates++] = be16 (data + c + 0x14) == 0xffff ? -1 : (int)be16 (data + c + 0x14);
		else if (ocn_tag (data + c, "BONE") && n_bones < OCN_MAX_BONES && n >= c + 0x80)
		{
			ocn_bone_t *b = bones + n_bones;
			b->parent = be16 (data + c + 0x0a) == 0xffff ? -1 : (int)be16 (data + c + 0x0a);
			if (b->parent >= (int)n_bones)
				b->parent = -1;
			for (uint i = 0; i < 4; i++)
				b->q[i] = bef32 (data + c + 0x10 + 4 * i);
			for (uint i = 0; i < 3; i++)
			{
				b->s[i] = bef32 (data + c + 0x20 + 4 * i);
				b->t[i] = bef32 (data + c + 0x30 + 4 * i);
			}
			if (ocn_tag (data + c + 0x40, "NAME") && c + 0x60 + 32 <= n)
				snprintf (b->name, sizeof (b->name), "%.31s", (ccp)data + c + 0x60);
			if (!*b->name)
				snprintf (b->name, sizeof (b->name), "bone%u", n_bones);
			ocn_bone_world (b, bones);
			n_bones++;
		}
	}

	model.joints = CALLOC (n_bones ? n_bones : 1, sizeof (*model.joints));
	model.num_joints = n_bones;
	for (uint i = 0; i < n_bones; i++)
	{
		joint_t *j = model.joints + i;
		StringCopyS (j->name, sizeof (j->name), bones[i].name);
		j->parent_idx = bones[i].parent;
		j->translate = (vec3_t) { bones[i].t[0], bones[i].t[1], bones[i].t[2] };
		j->rotate = ocn_euler_deg (bones[i].q);
		j->scale = (vec3_t) { bones[i].s[0], bones[i].s[1], bones[i].s[2] };
	}

	model.materials = CALLOC (n_mates ? n_mates : 1, sizeof (*model.materials));
	model.num_materials = n_mates;
	for (uint i = 0; i < n_mates; i++)
	{
		material_t *m = model.materials + i;
		snprintf (m->name, sizeof (m->name), "mat_%u", i);
		m->diffuse[0] = m->diffuse[1] = m->diffuse[2] = m->diffuse[3] = 1.0f;
		if (mate_tex[i] >= 0)
		{
			snprintf (m->textures[0], sizeof (m->textures[0]), "%s.tex%03d", tex_base, mate_tex[i]);
			m->num_textures = 1;
			m->wrap_s[0] = m->wrap_t[0] = 1; // repeat: UVs run far past 1
			m->min_filter[0] = m->mag_filter[0] = 1;
		}
	}

	// pass 2: shapes
	uint cap_mesh = 0, cap_inf = 0, n_shapes = 0;
	for (uint c = mdel + OCN_HDR, n; c && c < mdel_end; c = n)
	{
		n = ocn_next (data, mdel_end, c);
		if (!n)
			break;
		if (!ocn_tag (data + c, "SHAP"))
			continue;
		const uint shape_idx = n_shapes++;

		// element banks (UVs) in order, then the meshes that use them
		const u8 *txcd[64], *vert[64], *norm[64];
		uint n_txcd[64], n_vert[64], n_elem = 0, n_shape_mesh = 0;
		const uint owner = be16 (data + c + 0x14); // bone the shape is attached to
		for (uint e = c + OCN_HDR, en; e && e < n; e = en)
		{
			en = ocn_next (data, n, e);
			if (!en)
				break;
			if (ocn_tag (data + e, "ELEM") && n_elem < 64)
			{
				txcd[n_elem] = vert[n_elem] = norm[n_elem] = 0;
				n_txcd[n_elem] = 0;
				n_vert[n_elem] = be16 (data + e + 0x12);
				for (uint t = e + OCN_HDR, tn; t && t < en; t = tn)
				{
					tn = ocn_next (data, en, t);
					if (!tn || ocn_tag (data + t, "ENDE"))
						break;
					if (ocn_tag (data + t, "VERT") && tn - t >= OCN_HDR + 12 * (u64)n_vert[n_elem])
						vert[n_elem] = data + t + OCN_HDR;
					else if (ocn_tag (data + t, "NORM")
						&& tn - t >= OCN_HDR + 12 * (u64)n_vert[n_elem])
						norm[n_elem] = data + t + OCN_HDR;
					else if (ocn_tag (data + t, "TXCD"))
					{
						txcd[n_elem] = data + t + OCN_HDR;
						n_txcd[n_elem] = (tn - t - OCN_HDR) / 4;
					}
				}
				n_elem++;
			}
			else if (ocn_tag (data + e, "MESH") && en >= e + 2 * OCN_HDR)
			{
				const uint mi = n_shape_mesh++;
				const uint mate = be16 (data + e + 0x16);
				const uint prim = e + OCN_HDR;
				if (!ocn_tag (data + prim, "PRIM"))
					continue;
				const uint prim_end = prim + ocn_end (data + prim) < en ? prim + ocn_end (data + prim) : en;
				uint vidl = 0, weig = 0;
				for (uint p = prim + OCN_HDR, pn; p && p < prim_end; p = pn)
				{
					pn = ocn_next (data, prim_end, p);
					if (!pn)
						break;
					if (ocn_tag (data + p, "VIDL"))
						vidl = p;
					else if (ocn_tag (data + p, "WEIG"))
						weig = p;
				}
				if (!vidl)
					continue;

				uint n_rec = 0;
				ocn_rec_t *rec = 0;
				if (weig)
					rec = ocn_read_weig (bones, n_bones, data, weig, &n_rec);
				else if (mi < n_elem && vert[mi] && owner < n_bones && n_vert[mi])
				{
					// rigid shape: float vertex banks local to the owning bone
					n_rec = n_vert[mi];
					rec = CALLOC (n_rec, sizeof (*rec));
					const float *m = bones[owner].w;
					for (uint v = 0; v < n_rec; v++)
					{
						float p[3], nn[3] = { 0, 0, 0 };
						for (uint j = 0; j < 3; j++)
						{
							p[j] = bef32 (vert[mi] + 12 * v + 4 * j);
							if (norm[mi])
								nn[j] = bef32 (norm[mi] + 12 * v + 4 * j);
						}
						rec[v].n = 1;
						rec[v].e[0].bone = (int)owner;
						rec[v].e[0].weight = 1.0f;
						for (uint j = 0; j < 3; j++)
						{
							rec[v].pos[j] = m[j * 4] * p[0] + m[j * 4 + 1] * p[1] + m[j * 4 + 2] * p[2] + m[j * 4 + 3];
							rec[v].nrm[j] = m[j * 4] * nn[0] + m[j * 4 + 1] * nn[1] + m[j * 4 + 2] * nn[2];
						}
					}
				}
				ocn_tri_t *tri = 0;
				uint n_tri = 0, cap_tri = 0;
				ocn_display_list (data + vidl + OCN_HDR, ocn_end (data + vidl) - OCN_HDR, &tri, &n_tri, &cap_tri);
				if (!rec || !n_tri)
				{
					FREE (rec);
					FREE (tri);
					continue;
				}

				if (model.num_meshes == cap_mesh)
				{
					cap_mesh = cap_mesh ? cap_mesh * 2 : 32;
					model.meshes = REALLOC (model.meshes, cap_mesh * sizeof (*model.meshes));
				}
				mesh_t *me = model.meshes + model.num_meshes++;
				memset (me, 0, sizeof (*me));
				snprintf (me->name, sizeof (me->name), "shape%u_mesh%u", shape_idx, mi);
				me->material_idx = mate < n_mates ? (int)mate : -1;

				// one node influence per pool vertex
				const size_t ni_base = model.num_node_influences;
				if (ni_base + n_rec > cap_inf)
				{
					cap_inf = (ni_base + n_rec) * 2;
					model.node_influences
						= REALLOC (model.node_influences, cap_inf * sizeof (*model.node_influences));
				}
				for (uint v = 0; v < n_rec; v++)
				{
					node_influence_t *ni = model.node_influences + ni_base + v;
					ni->num_weights = rec[v].n;
					ni->weights = CALLOC (rec[v].n ? rec[v].n : 1, sizeof (*ni->weights));
					for (uint i = 0; i < rec[v].n; i++)
					{
						ni->weights[i].bone_idx = rec[v].e[i].bone;
						ni->weights[i].weight = rec[v].e[i].weight;
					}
				}
				model.num_node_influences += n_rec;

				const uint nv = 3 * n_tri;
				me->num_positions = me->num_normals = me->num_texcoords = me->num_vertices = nv;
				me->positions = CALLOC (nv, sizeof (*me->positions));
				me->normals = CALLOC (nv, sizeof (*me->normals));
				me->texcoords = CALLOC (nv, sizeof (*me->texcoords));
				me->position_node = CALLOC (nv, sizeof (*me->position_node));
				me->vertices = CALLOC (nv, sizeof (*me->vertices));
				for (uint t = 0; t < n_tri; t++)
					for (uint i = 0; i < 3; i++)
					{
						const uint o = 3 * t + i;
						const u16 *iv = tri[t].v[i];
						const uint pi = iv[0] < n_rec ? iv[0] : 0;
						me->positions[o]
							= (vec3_t) { rec[pi].pos[0], rec[pi].pos[1], rec[pi].pos[2] };
						me->normals[o]
							= (vec3_t) { rec[pi].nrm[0], rec[pi].nrm[1], rec[pi].nrm[2] };
						if (mi < n_elem && txcd[mi] && iv[2] < n_txcd[mi])
							me->texcoords[o] = (vec2_t) { be16 (txcd[mi] + 4 * iv[2]) / 1024.0f,
								be16 (txcd[mi] + 4 * iv[2] + 2) / 1024.0f };
						me->position_node[o] = (int)(ni_base + pi);
						me->vertices[o].position_idx = (int)o;
						me->vertices[o].normal_idx = (int)o;
						me->vertices[o].tangent_idx = -1;
						me->vertices[o].texcoord_idx = (int)o;
						me->vertices[o].matrix_idx = -1;
						me->vertices[o].color_idx[0] = me->vertices[o].color_idx[1] = -1;
						for (uint k = 0; k < 7; k++)
							me->vertices[o].extra_texcoord_idx[k] = -1;
					}
				FREE (rec);
				FREE (tri);
			}
		}
	}

	int ret = 0;
	if (model.num_meshes)
	{
		CreatePath (dir, true);
		ComputeModelTRSBinds (&model);
		ret = ExportModelToGLB (&model, out_glb_file) == 0 ? (int)model.num_meshes : -1;
	}
	ocn_free_model (&model);
	FREE (bones);
	FREE (mate_tex);
	return ret;
}
