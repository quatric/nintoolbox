// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Blitz Games "Babel" .rev packages (see lib-rev.h).
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-rev.h"
#include "lib-nintendo.h"
#include "lib-excite.h"
#include <math.h>
#include <string.h>

#define REV_HEADER_SIZE 0x30
#define REV_MAX_ENTRIES 0x100000
#define REV_MAX_NAME 1024

static inline uint rev_be16 (const u8 *p)
{
	return (uint)p[0] << 8 | p[1];
}

static inline float rev_bef32 (const u8 *p)
{
	const u32 u = rd_be32 (p);
	float f;
	memcpy (&f, &u, sizeof (f));
	return f;
}

//-----------------------------------------------------------------------------
///////////////			container			///////////////
//-----------------------------------------------------------------------------

static u32 rev_crc_tab[256];
static bool rev_crc_init = false;

static void rev_make_crc_tab (void)
{
	for (uint i = 0; i < 256; i++)
	{
		u32 c = (u32)i << 24;
		for (int k = 0; k < 8; k++)
			c = (c & 0x80000000u) ? (c << 1) ^ 0x04c11db7u : c << 1;
		rev_crc_tab[i] = c;
	}
	rev_crc_init = true;
}

u32 RevNameCRC (ccp name)
{
	if (!rev_crc_init)
		rev_make_crc_tab ();
	u32 crc = 0;
	for (const u8 *p = (const u8 *)name; *p; p++)
	{
		const u8 ch = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
		crc = (crc << 8) ^ rev_crc_tab[(crc >> 24) ^ ch];
	}
	return crc;
}

enumError ScanREV (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < REV_HEADER_SIZE)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 align = rd_be32 (data + 4);
	const u32 count = rd_be32 (data + 12);
	if (align < 0x20 || align > 0x10000 || (align & (align - 1)) || !count || count > REV_MAX_ENTRIES)
		return EINVAL;
	const u64 index = (u64)rd_be32 (data + 0x10) * align;
	const u64 names = (u64)rd_be32 (data + 0x28) * align;
	const u32 names_len = rd_be32 (data + 0x2c);
	if (index < REV_HEADER_SIZE || index + (u64)count * 32 > size || !names_len
		|| names + names_len > size)
		return EINVAL;

	// The game binary-searches the index by CRC, so it is strictly ordered
	// and every payload lies inside the file: a cheap, strong validation.
	const u8 *ix = data + index;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = ix + i * 32;
		if ((u64)rd_be32 (e) * align + rd_be32 (e + 8) > size)
			return EINVAL;
		if (i && rd_be32 (e + 4) <= rd_be32 (e - 32 + 4))
			return EINVAL;
	}

	nintendo_sarc_entry_t *out = CALLOC (count, sizeof (*out));
	bool *used = CALLOC (count, sizeof (*used));
	if (!out || !used)
	{
		FREE (out);
		FREE (used);
		return ERR_OUT_OF_MEMORY;
	}
	// Names are stored in file order; resolve each to its entry by CRC. Slot
	// order of the result follows the name table, then the unnamed entries.
	uint *slot = CALLOC (count, sizeof (*slot));
	uint n = 0;
	char *tab = MALLOC ((size_t)names_len + 1);
	if (!slot || !tab)
	{
		FREE (out);
		FREE (used);
		FREE (slot);
		FREE (tab);
		return ERR_OUT_OF_MEMORY;
	}
	memcpy (tab, data + names, names_len);
	tab[names_len] = 0;
	const char **nm = CALLOC (count, sizeof (*nm));
	if (!nm)
	{
		FREE (out);
		FREE (used);
		FREE (slot);
		FREE (tab);
		return ERR_OUT_OF_MEMORY;
	}
	for (char *p = tab; p < tab + names_len && n < count;)
	{
		const size_t len = strlen (p);
		if (len)
		{
			const u32 crc = RevNameCRC (p);
			uint lo = 0, hi = count;
			while (lo < hi)
			{
				const uint mid = (lo + hi) / 2;
				if (rd_be32 (ix + mid * 32 + 4) < crc)
					lo = mid + 1;
				else
					hi = mid;
			}
			if (lo < count && rd_be32 (ix + lo * 32 + 4) == crc && !used[lo])
			{
				used[lo] = true;
				slot[n] = lo;
				nm[n++] = p;
			}
		}
		p += len + 1;
	}

	bool ok = true;
	uint total = 0;
	char nbuf[REV_MAX_NAME + 16];
	for (uint pass = 0; ok && pass < 2; pass++)
		for (uint i = 0; ok && i < (pass ? count : n); i++)
		{
			if (pass && used[i])
				continue;
			const u8 *e = ix + (pass ? i : slot[i]) * 32;
			const u8 *payload = data + (u64)rd_be32 (e) * align;
			const uint psize = rd_be32 (e + 8);
			if (pass)
				snprintf (nbuf, sizeof (nbuf), "%08x", rd_be32 (e + 4));
			else if (nm[i] && *nm[i] && OwnedNameOk (nm[i]))
				snprintf (nbuf, sizeof (nbuf), "%s", nm[i]);
			else
				snprintf (nbuf, sizeof (nbuf), "%08x", rd_be32 (e + 4));
			// give the game resources a suffix the extractor recognises
			ccp ext = IsBlitzTexture (payload, psize) ? ".bltex"
					: IsBlitzActor (payload, psize)	  ? ".blact"
					: pass							  ? ".bin"
													  : "";
			const size_t l = strlen (nbuf);
			if (l + strlen (ext) < sizeof (nbuf))
				strcpy (nbuf + l, ext);
			ok = OwnedEntryAdd (out, total, nbuf, payload, psize);
			total += ok;
		}
	FREE (used);
	FREE (slot);
	FREE (nm);
	FREE (tab);
	if (!ok)
	{
		ResetOwnedEntries (out, total);
		return ERR_CANT_CREATE;
	}
	*entries = out;
	*n_entries = total;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////			textures			///////////////
//-----------------------------------------------------------------------------

typedef struct
{
	uint gx, pal_fmt, pal_count;
} rev_texfmt_t;

// bUploadTexture's format switch; PAL_COUNT 0 = not paletted.
static bool rev_texfmt (uint fmt, rev_texfmt_t *f)
{
	switch (fmt)
	{
		case 15: *f = (rev_texfmt_t){ 6, 0, 0 }; return true;
		case 16: *f = (rev_texfmt_t){ 5, 0, 0 }; return true;
		case 17: *f = (rev_texfmt_t){ 9, 1, 256 }; return true;
		case 18: *f = (rev_texfmt_t){ 9, 2, 256 }; return true;
		case 19: *f = (rev_texfmt_t){ 8, 1, 16 }; return true;
		case 20: *f = (rev_texfmt_t){ 8, 2, 16 }; return true;
		case 21: *f = (rev_texfmt_t){ 14, 0, 0 }; return true;
		case 22: *f = (rev_texfmt_t){ 0, 0, 0 }; return true;
		case 23: *f = (rev_texfmt_t){ 4, 0, 0 }; return true;
		case 29:
		case 30: *f = (rev_texfmt_t){ 1, 0, 0 }; return true;
	}
	return false;
}

bool IsBlitzTexture (const u8 *d, uint size)
{
	if (!d || size < 0xa0)
		return false;
	for (uint i = 0; i < 0x20; i++)
		if (d[i])
			return false;
	const u32 w = rd_be32 (d + 0x20), h = rd_be32 (d + 0x24);
	rev_texfmt_t f;
	if (!w || !h || w > 2048 || h > 2048 || (w & (w - 1)) || (h & (h - 1))
		|| !rev_texfmt (rd_be32 (d + 0x28), &f))
		return false;
	const u32 pix = rd_be32 (d + 0x70), pal = rd_be32 (d + 0x6c);
	if (pix < 0xa0 || pix >= size)
		return false;
	return !f.pal_count || (pal >= 0xa0 && pal + f.pal_count * 2 <= pix);
}

enumError DecodeBlitzTexture (u8 **rgba, uint *width, uint *height, const u8 *d, uint size)
{
	if (!IsBlitzTexture (d, size))
		return ERR_NOTHING_TO_DO;
	rev_texfmt_t f;
	rev_texfmt (rd_be32 (d + 0x28), &f);
	const uint w = rd_be32 (d + 0x20), h = rd_be32 (d + 0x24);
	const uint pix = rd_be32 (d + 0x70), pal = rd_be32 (d + 0x6c);
	const enumError err = DecodeGXTexture_RGBA (rgba, w, h, f.gx, d + pix, size - pix,
		f.pal_count ? d + pal : 0, f.pal_count, f.pal_fmt);
	if (!err)
	{
		*width = w;
		*height = h;
	}
	return err;
}

//-----------------------------------------------------------------------------
///////////////			actors				///////////////
//-----------------------------------------------------------------------------

#define BL_NODE_SIZE 0x134
#define BL_MAX_NODES 4096

bool IsBlitzActor (const u8 *d, uint size)
{
	if (!d || size < 0x140 || rd_be32 (d) || rd_be32 (d + 4) != 0x100)
		return false;
	const u32 root = rd_be32 (d + 0xa0);
	if (root < 0x100 || (u64)root + BL_NODE_SIZE > size)
		return false;
	const uint type = d[root + 0x70];
	return type >= 1 && type <= 7;
}

// Row-major 3x4 affine.
typedef struct
{
	float m[12];
} bl_mat_t;

static bl_mat_t bl_mul (const bl_mat_t *a, const bl_mat_t *b)
{
	bl_mat_t r;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 4; j++)
			r.m[i * 4 + j] = a->m[i * 4] * b->m[j] + a->m[i * 4 + 1] * b->m[4 + j]
				+ a->m[i * 4 + 2] * b->m[8 + j] + (j == 3 ? a->m[i * 4 + 3] : 0.0f);
	return r;
}

static bl_mat_t bl_local (const u8 *n)
{
	float qx = rev_bef32 (n + 0x20), qy = rev_bef32 (n + 0x24), qz = rev_bef32 (n + 0x28),
		  qw = rev_bef32 (n + 0x2c);
	const float len = sqrtf (qx * qx + qy * qy + qz * qz + qw * qw);
	if (len > 1e-6f)
	{
		qx /= len;
		qy /= len;
		qz /= len;
		qw /= len;
	}
	else
		qx = qy = qz = 0, qw = 1;
	const float sx = rev_bef32 (n + 0x50), sy = rev_bef32 (n + 0x54), sz = rev_bef32 (n + 0x58);
	bl_mat_t r = { { 1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qz * qw), 2 * (qx * qz + qy * qw), 0,
		2 * (qx * qy + qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qx * qw), 0,
		2 * (qx * qz - qy * qw), 2 * (qy * qz + qx * qw), 1 - 2 * (qx * qx + qy * qy), 0 } };
	for (int i = 0; i < 3; i++)
	{
		r.m[i * 4] *= sx;
		r.m[i * 4 + 1] *= sy;
		r.m[i * 4 + 2] *= sz;
	}
	r.m[3] = rev_bef32 (n);
	r.m[7] = rev_bef32 (n + 4);
	r.m[11] = rev_bef32 (n + 8);
	return r;
}

// One source of GX arrays + indexed display lists: a static mesh node or the
// soft skinned actor header.
typedef struct
{
	u32 pos, nrm, tex, clr, dl, tab, batches, nbatches, prims, pstride, nrm_stride;
	bl_mat_t mat;
	const char *name;
} bl_src_t;

typedef struct
{
	u16 p, n, t;
} bl_corner_t;

typedef struct
{
	bl_corner_t *v;
	size_t num, cap;
} bl_corners_t;

static bool bl_push (bl_corners_t *c, bl_corner_t v)
{
	if (c->num == c->cap)
	{
		const size_t cap = c->cap ? c->cap * 2 : 256;
		bl_corner_t *nv = REALLOC (c->v, cap * sizeof (*nv));
		if (!nv)
			return false;
		c->v = nv;
		c->cap = cap;
	}
	c->v[c->num++] = v;
	return true;
}

// GX strip/fan/triangle expansion, same winding as the other GX importers.
static bool bl_emit (bl_corners_t *out, uint op, const bl_corner_t *v, uint n)
{
	if (op == 0x90)
	{
		for (uint i = 0; i + 2 < n; i += 3)
			if (!bl_push (out, v[i]) || !bl_push (out, v[i + 1]) || !bl_push (out, v[i + 2]))
				return false;
		return true;
	}
	if (op == 0xa0)
	{
		for (uint i = 2; i < n; i++)
			if (!bl_push (out, v[i - 1]) || !bl_push (out, v[i]) || !bl_push (out, v[0]))
				return false;
		return true;
	}
	if (op != 0x98)
		return true;
	for (uint i = 2; i < n; i++)
	{
		// GX strips alternate winding; reversed so front faces are CCW for glTF
		const bl_corner_t a = v[i];
		const bl_corner_t b = (i & 1) ? v[i - 2] : v[i - 1];
		const bl_corner_t c = (i & 1) ? v[i - 1] : v[i - 2];
		if (!bl_push (out, a) || !bl_push (out, b) || !bl_push (out, c))
			return false;
	}
	return true;
}

typedef struct
{
	model_t *model;
	const u8 *d;
	uint size;
	BlitzTexNameFunc texname;
	void *ctx;
	u32 *mat_crc; // parallel to model->materials
} bl_ctx_t;

static int bl_material (bl_ctx_t *c, u32 crc)
{
	model_t *m = c->model;
	for (size_t i = 0; i < m->num_materials; i++)
		if (c->mat_crc[i] == crc)
			return (int)i;
	material_t *nm = REALLOC (m->materials, (m->num_materials + 1) * sizeof (*nm));
	if (!nm)
		return -1;
	m->materials = nm;
	u32 *nc = REALLOC (c->mat_crc, (m->num_materials + 1) * sizeof (*nc));
	if (!nc)
		return -1;
	c->mat_crc = nc;
	material_t *mt = m->materials + m->num_materials;
	memset (mt, 0, sizeof (*mt));
	snprintf (mt->name, sizeof (mt->name), "Material%u", (uint)m->num_materials);
	mt->diffuse[0] = mt->diffuse[1] = mt->diffuse[2] = mt->diffuse[3] = 1.0f;
	ccp tn = crc && c->texname ? c->texname (c->ctx, crc) : 0;
	if (tn)
	{
		snprintf (mt->textures[0], sizeof (mt->textures[0]), "%s", tn);
		mt->num_textures = 1;
		mt->wrap_s[0] = mt->wrap_t[0] = 1;
		mt->min_filter[0] = mt->mag_filter[0] = 1;
		mt->has_alpha = 1;
	}
	c->mat_crc[m->num_materials] = crc;
	return (int)m->num_materials++;
}

static bool bl_range (const bl_ctx_t *c, u64 off, u64 len)
{
	return off && off + len <= c->size;
}

// Build one mesh_t per batch of SRC and append it to the model.
static bool bl_add_src (bl_ctx_t *c, const bl_src_t *s)
{
	const u8 *d = c->d;
	if (!s->nbatches || s->nbatches > 4096 || !s->dl || !s->tab || !s->batches || !s->prims
		|| !s->pos || !bl_range (c, s->batches, (u64)s->nbatches * 16))
		return false;

	uint pi = 0;
	for (uint b = 0; b < s->nbatches; b++)
	{
		const u8 *bp = d + s->batches + (u64)b * 16;
		const uint np = rd_be32 (bp);
		const u32 crc = rd_be32 (bp + 4);
		bl_corners_t cor = { 0, 0, 0 };
		bool ok = true;
		uint maxp = 0, maxn = 0, maxt = 0;
		for (uint k = 0; k < np && ok; k++, pi++)
		{
			if (!bl_range (c, s->prims + (u64)pi * s->pstride, s->pstride)
				|| !bl_range (c, s->tab + (u64)pi * 8, 8))
			{
				ok = false;
				break;
			}
			const u8 *tp = d + s->tab + (u64)pi * 8;
			const u32 off = rd_be32 (tp), len = rd_be32 (tp + 4);
			if ((u64)s->dl + off + len > c->size)
			{
				ok = false;
				break;
			}
			const u8 *q = d + s->dl + off, *end = q + len;
			while (q < end && !*q)
				q++;
			if (q + 3 > end)
				continue;
			const uint op = *q & 0xf8, n = rev_be16 (q + 1);
			q += 3;
			if (!n || (u64)(end - q) < (u64)n * 8 || (op != 0x98 && op != 0x90 && op != 0xa0))
				continue;
			bl_corner_t *v = MALLOC (n * sizeof (*v));
			if (!v)
			{
				ok = false;
				break;
			}
			for (uint i = 0; i < n; i++, q += 8)
			{
				// indices: position, normal, colour, texcoord
				v[i].p = rev_be16 (q);
				v[i].n = rev_be16 (q + 2);
				v[i].t = rev_be16 (q + 6);
				if (v[i].p > maxp)
					maxp = v[i].p;
				if (v[i].n > maxn)
					maxn = v[i].n;
				if (v[i].t > maxt)
					maxt = v[i].t;
			}
			ok = bl_emit (&cor, op, v, n);
			FREE (v);
		}
		if (!ok || !cor.num)
		{
			FREE (cor.v);
			if (!ok)
				return false;
			continue;
		}

		const bool has_n = s->nrm != 0, has_t = s->tex != 0;
		if (!bl_range (c, s->pos, (u64)(maxp + 1) * 12)
			|| (has_n && !bl_range (c, s->nrm, (u64)(maxn + 1) * s->nrm_stride))
			|| (has_t && !bl_range (c, s->tex, (u64)(maxt + 1) * 8)))
		{
			FREE (cor.v);
			return false;
		}

		mesh_t *nm = REALLOC (c->model->meshes, (c->model->num_meshes + 1) * sizeof (*nm));
		if (!nm)
		{
			FREE (cor.v);
			return false;
		}
		c->model->meshes = nm;
		mesh_t *mesh = c->model->meshes + c->model->num_meshes;
		memset (mesh, 0, sizeof (*mesh));
		snprintf (mesh->name, sizeof (mesh->name), "%s_%u", s->name, b);
		const int mat = bl_material (c, crc);
		mesh->material_idx = mat < 0 ? 0 : mat;
		mesh->num_positions = maxp + 1;
		mesh->positions = CALLOC (mesh->num_positions, sizeof (*mesh->positions));
		mesh->vertices = CALLOC (cor.num, sizeof (*mesh->vertices));
		if (has_n)
		{
			mesh->num_normals = maxn + 1;
			mesh->normals = CALLOC (mesh->num_normals, sizeof (*mesh->normals));
		}
		if (has_t)
		{
			mesh->num_texcoords = maxt + 1;
			mesh->texcoords = CALLOC (mesh->num_texcoords, sizeof (*mesh->texcoords));
		}
		if (!mesh->positions || !mesh->vertices || (has_n && !mesh->normals)
			|| (has_t && !mesh->texcoords))
		{
			FREE (cor.v);
			return false;
		}
		c->model->num_meshes++;

		const float *M = s->mat.m;
		for (size_t i = 0; i < mesh->num_positions; i++)
		{
			const u8 *p = d + s->pos + i * 12;
			const float x = rev_bef32 (p), y = rev_bef32 (p + 4), z = rev_bef32 (p + 8);
			mesh->positions[i].x = M[0] * x + M[1] * y + M[2] * z + M[3];
			mesh->positions[i].y = M[4] * x + M[5] * y + M[6] * z + M[7];
			mesh->positions[i].z = M[8] * x + M[9] * y + M[10] * z + M[11];
		}
		for (size_t i = 0; has_n && i < mesh->num_normals; i++)
		{
			const int8_t *p = (const int8_t *)(d + s->nrm + i * s->nrm_stride);
			const float x = p[0] / 64.0f, y = p[1] / 64.0f, z = p[2] / 64.0f;
			float nx = M[0] * x + M[1] * y + M[2] * z, ny = M[4] * x + M[5] * y + M[6] * z,
				  nz = M[8] * x + M[9] * y + M[10] * z;
			const float l = sqrtf (nx * nx + ny * ny + nz * nz);
			if (l > 1e-6f)
				nx /= l, ny /= l, nz /= l;
			mesh->normals[i] = (vec3_t){ nx, ny, nz };
		}
		for (size_t i = 0; has_t && i < mesh->num_texcoords; i++)
		{
			const u8 *p = d + s->tex + i * 8;
			mesh->texcoords[i] = (vec2_t){ rev_bef32 (p), rev_bef32 (p + 4) };
		}
		mesh->num_vertices = cor.num;
		for (size_t i = 0; i < cor.num; i++)
		{
			vertex_t *v = mesh->vertices + i;
			v->position_idx = cor.v[i].p;
			v->normal_idx = has_n ? cor.v[i].n : -1;
			v->tangent_idx = -1;
			v->texcoord_idx = has_t ? cor.v[i].t : -1;
			v->matrix_idx = -1;
			v->color_idx[0] = v->color_idx[1] = -1;
			for (int e = 0; e < 7; e++)
				v->extra_texcoord_idx[e] = -1;
		}
		FREE (cor.v);
	}
	return true;
}

static void bl_walk (bl_ctx_t *c, u32 node, const bl_mat_t *parent, uint *count, bool *any)
{
	const u8 *d = c->d;
	const u32 first = node;
	for (u32 n = node; n && (u64)n + BL_NODE_SIZE <= c->size; n = rd_be32 (d + n + 0x110))
	{
		if (++*count > BL_MAX_NODES)
			return;
		const bl_mat_t local = bl_local (d + n);
		const bl_mat_t world = bl_mul (parent, &local);
		if (d[n + 0x70] == 2)
		{
			const u8 *m = d + n + 0x80;
			const u32 flags = rd_be32 (m + 0x4c);
			bl_src_t s = { rd_be32 (m + 0x50), rd_be32 (m + 0x54), rd_be32 (m + 0x58),
				rd_be32 (m + 0x5c), rd_be32 (m + 0x60), rd_be32 (m + 0x68), rd_be32 (m + 0x0c),
				rd_be32 (m + 0x08), rd_be32 (m + 0x10), 8, (flags & 0x100) ? 9u : 3u, world, 0 };
			char name[32];
			snprintf (name, sizeof (name), "Node%u", rd_be32 (d + n + 0x74));
			s.name = name;
			if ((flags & 0x10) && rd_be32 (m) && bl_add_src (c, &s))
				*any = true;
		}
		const u32 child = rd_be32 (d + n + 0x11c);
		if (child)
			bl_walk (c, child, &world, count, any);
		if (rd_be32 (d + n + 0x110) == first)
			break;
	}
}

model_t *ParseBlitzActor (const u8 *d, uint size, BlitzTexNameFunc texname, void *ctx)
{
	if (!IsBlitzActor (d, size))
		return 0;
	model_t *model = CALLOC (1, sizeof (*model));
	if (!model)
		return 0;
	bl_ctx_t c = { model, d, size, texname, ctx, 0 };
	bool any = false;
	static const bl_mat_t ident = { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 } };

	if (rd_be32 (d + 0xa4) & 1)
	{
		// soft skinned: mesh data lives in the actor header (bind pose only)
		if (rev_be16 (d + 0x52) & 2)
		{
			const bl_src_t s = { rd_be32 (d + 0x60), rd_be32 (d + 0x64), rd_be32 (d + 0x68),
				rd_be32 (d + 0x6c), rd_be32 (d + 0x58), rd_be32 (d + 0x54), rd_be32 (d + 0x2c),
				rd_be32 (d + 0x28), rd_be32 (d + 0x30), 18, 3, ident, "Skin" };
			any = bl_add_src (&c, &s);
		}
	}
	else
	{
		uint count = 0;
		bl_walk (&c, rd_be32 (d + 0xa0), &ident, &count, &any);
	}
	if (!any || !model->num_meshes)
	{
		FreeModel (model);
		FREE (c.mat_crc);
		return 0;
	}
	FREE (c.mat_crc);
	return model;
}
