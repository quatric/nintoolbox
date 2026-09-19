// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Gamebryo .nif reader (see lib-nif.h).
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-nif.h"
#include "lib-excite.h"
#include <math.h>
#include <string.h>

#define NIF_MAX_BLOCKS 0x40000
#define NIF_MAX_STRINGS 0x40000
#define NIF_MAX_DEPTH 64
#define NIF_NONE 0xffffffffu

static inline uint nbe16 (const u8 *p) { return (uint)p[0] << 8 | p[1]; }
static inline u32 nbe32 (const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static inline float nbef (const u8 *p)
{
	const u32 u = nbe32 (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

typedef struct
{
	uint type; // index into type names
	u32 off, size;
} nif_block_t;

typedef struct
{
	uint tex_block; // NiSourceTexture block
	uint data_block; // NiPersistentSrcTextureRendererData block
	char name[96];
} nif_tex_t;

struct nif_t
{
	const u8 *d;
	uint size;
	uint n_blocks, n_types, n_strings;
	char **types; // base names (template arguments cut)
	char **strings;
	nif_block_t *blocks;
	uint n_roots;
	u32 roots_off;
	nif_tex_t *tex;
	uint n_tex;
};

//-----------------------------------------------------------------------------
///////////////			bounds-checked cursor			///////////////
//-----------------------------------------------------------------------------

typedef struct
{
	const u8 *p, *end;
	bool ok;
} cur_t;

static u32 cu32 (cur_t *c)
{
	if (c->p + 4 > c->end)
	{
		c->ok = false;
		return 0;
	}
	const u32 v = nbe32 (c->p);
	c->p += 4;
	return v;
}

static uint cu16 (cur_t *c)
{
	if (c->p + 2 > c->end)
	{
		c->ok = false;
		return 0;
	}
	const uint v = nbe16 (c->p);
	c->p += 2;
	return v;
}

static uint cu8 (cur_t *c)
{
	if (c->p + 1 > c->end)
	{
		c->ok = false;
		return 0;
	}
	return *c->p++;
}

static float cf32 (cur_t *c)
{
	const u32 u = cu32 (c);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

static void cskip (cur_t *c, u64 n)
{
	if ((u64)(c->end - c->p) < n)
	{
		c->ok = false;
		c->p = c->end;
	}
	else
		c->p += n;
}

//-----------------------------------------------------------------------------
///////////////			container			///////////////
//-----------------------------------------------------------------------------

void NifClose (nif_t *nif)
{
	if (!nif)
		return;
	for (uint i = 0; i < nif->n_types; i++)
		FREE (nif->types[i]);
	for (uint i = 0; i < nif->n_strings; i++)
		FREE (nif->strings[i]);
	FREE (nif->types);
	FREE (nif->strings);
	FREE (nif->blocks);
	FREE (nif->tex);
	FREE (nif);
}

static char *dup_n (const u8 *s, uint n, bool cut_template)
{
	char *r = MALLOC (n + 1);
	if (!r)
		return 0;
	memcpy (r, s, n);
	r[n] = 0;
	if (cut_template)
	{
		char *t = strchr (r, 1);
		if (t)
			*t = 0;
	}
	return r;
}

static ccp nif_type (const nif_t *n, uint block)
{
	return block < n->n_blocks ? n->types[n->blocks[block].type] : "";
}

static ccp nif_string (const nif_t *n, u32 idx)
{
	return idx < n->n_strings ? n->strings[idx] : 0;
}

static const u8 *blk (const nif_t *n, uint b, uint *size)
{
	if (b >= n->n_blocks)
		return 0;
	*size = n->blocks[b].size;
	return n->d + n->blocks[b].off;
}

static void nif_find_textures (nif_t *n);

nif_t *NifOpen (const u8 *d, uint size)
{
	static const char magic[] = "Gamebryo File Format, Version 20.6.0.0\n";
	if (!d || size < 0x60 || memcmp (d, magic, sizeof (magic) - 1))
		return 0;
	cur_t c = { d + sizeof (magic) - 1, d + size, true };
	// the first fields are little-endian, the rest follows the endian flag
	if (c.p + 13 > c.end)
		return 0;
	const u32 ver = c.p[0] | c.p[1] << 8 | c.p[2] << 16 | (u32)c.p[3] << 24;
	if (ver != 0x14060000 || c.p[4] != 0)
		return 0;
	const u32 nb = c.p[9] | c.p[10] << 8 | c.p[11] << 16 | (u32)c.p[12] << 24;
	c.p += 13;
	if (!nb || nb > NIF_MAX_BLOCKS)
		return 0;

	nif_t *n = CALLOC (1, sizeof (*n));
	if (!n)
		return 0;
	n->d = d;
	n->size = size;
	n->n_blocks = nb;
	n->n_types = cu16 (&c);
	n->types = CALLOC (n->n_types + 1, sizeof (*n->types));
	n->blocks = CALLOC (nb, sizeof (*n->blocks));
	if (!n->types || !n->blocks)
		goto fail;
	for (uint i = 0; i < n->n_types && c.ok; i++)
	{
		const uint l = cu32 (&c);
		if (l > 256 || c.p + l > c.end)
		{
			c.ok = false;
			break;
		}
		n->types[i] = dup_n (c.p, l, true);
		c.p += l;
		if (!n->types[i])
			goto fail;
	}
	for (uint i = 0; i < nb && c.ok; i++)
	{
		n->blocks[i].type = cu16 (&c);
		if (n->blocks[i].type >= n->n_types)
			c.ok = false;
	}
	for (uint i = 0; i < nb && c.ok; i++)
		n->blocks[i].size = cu32 (&c);
	n->n_strings = cu32 (&c);
	cu32 (&c); // max string length
	if (!c.ok || n->n_strings > NIF_MAX_STRINGS)
		goto fail;
	n->strings = CALLOC (n->n_strings + 1, sizeof (*n->strings));
	if (!n->strings)
		goto fail;
	for (uint i = 0; i < n->n_strings && c.ok; i++)
	{
		const uint l = cu32 (&c);
		if (c.p + l > c.end)
		{
			c.ok = false;
			break;
		}
		n->strings[i] = dup_n (c.p, l, false);
		c.p += l;
		if (!n->strings[i])
			goto fail;
	}
	const uint ngroups = cu32 (&c);
	if (!c.ok || ngroups > 0x10000)
		goto fail;
	cskip (&c, 4ull * ngroups);
	if (!c.ok)
		goto fail;

	u64 off = c.p - d;
	for (uint i = 0; i < nb; i++)
	{
		n->blocks[i].off = (u32)off;
		off += n->blocks[i].size;
		if (off > size)
			goto fail;
	}
	if (off + 4 > size)
		goto fail;
	n->roots_off = (u32)off;
	n->n_roots = nbe32 (d + off);
	if (n->n_roots > 64 || off + 4 + 4ull * n->n_roots != size)
		goto fail;
	nif_find_textures (n);
	return n;

fail:
	NifClose (n);
	return 0;
}

//-----------------------------------------------------------------------------
///////////////			NiObjectNET / NiAVObject			///////////////
//-----------------------------------------------------------------------------

typedef struct
{
	u32 name;
	uint flags;
	float t[3], r[9], s;
	u32 props[16];
	uint n_props;
} nif_av_t;

static bool read_av (cur_t *c, nif_av_t *a)
{
	memset (a, 0, sizeof (*a));
	a->name = cu32 (c);
	const uint ne = cu32 (c);
	if (ne > 4096)
		return false;
	cskip (c, 4ull * ne);
	cu32 (c); // controller
	a->flags = cu16 (c);
	for (uint i = 0; i < 3; i++)
		a->t[i] = cf32 (c);
	for (uint i = 0; i < 9; i++)
		a->r[i] = cf32 (c);
	a->s = cf32 (c);
	const uint np = cu32 (c);
	if (np > 4096)
		return false;
	for (uint i = 0; i < np; i++)
	{
		const u32 r = cu32 (c);
		if (i < 16)
			a->props[a->n_props++] = r;
	}
	cu32 (c); // collision object
	return c->ok;
}

//-----------------------------------------------------------------------------
///////////////			textures			///////////////
//-----------------------------------------------------------------------------

#define PSRC_PALETTE 0x3b
#define PSRC_MIPS 0x3f

typedef struct
{
	uint fmt, w, h;
	const u8 *pix;
	uint pix_size;
} psrc_t;

static bool parse_psrc (const nif_t *n, uint b, psrc_t *t)
{
	uint size;
	const u8 *d = blk (n, b, &size);
	if (!d || size < PSRC_MIPS + 4 + 4 + 12 + 16)
		return false;
	t->fmt = nbe32 (d);
	const uint nm = nbe32 (d + PSRC_MIPS);
	if (!nm || nm > 16)
		return false;
	const u64 mip = PSRC_MIPS + 8;
	const u64 tail = mip + 12ull * nm;
	if (tail + 16 > size)
		return false;
	t->w = nbe32 (d + mip);
	t->h = nbe32 (d + mip + 4);
	const uint moff = nbe32 (d + mip + 8);
	const uint npix = nbe32 (d + tail);
	const uint faces = nbe32 (d + tail + 8);
	const uint platform = nbe32 (d + tail + 12);
	if (platform != 4 || faces != 1 || !t->w || !t->h || t->w > 4096 || t->h > 4096)
		return false;
	const u64 data = tail + 16;
	if (data + npix != size || moff >= npix)
		return false;
	t->pix = d + data + moff;
	t->pix_size = npix - moff;
	return true;
}

static void nif_find_textures (nif_t *n)
{
	uint cap = 0;
	for (uint b = 0; b < n->n_blocks; b++)
	{
		if (strcmp (nif_type (n, b), "NiSourceTexture"))
			continue;
		uint size;
		const u8 *d = blk (n, b, &size);
		cur_t c = { d, d + size, true };
		cu32 (&c);
		const uint ne = cu32 (&c);
		if (ne > 4096)
			continue;
		cskip (&c, 4ull * ne + 4);
		if (cu8 (&c) || !c.ok)
			continue; // external file reference
		const u32 fname = cu32 (&c);
		const u32 dref = cu32 (&c);
		psrc_t t;
		if (!c.ok || dref >= n->n_blocks
			|| strcmp (nif_type (n, dref), "NiPersistentSrcTextureRendererData")
			|| !parse_psrc (n, dref, &t))
			continue;
		if (n->n_tex == cap)
		{
			cap = cap ? cap * 2 : 8;
			nif_tex_t *nt = REALLOC (n->tex, cap * sizeof (*nt));
			if (!nt)
				return;
			n->tex = nt;
		}
		nif_tex_t *e = n->tex + n->n_tex++;
		e->tex_block = b;
		e->data_block = dref;
		ccp s = nif_string (n, fname);
		char stem[96];
		snprintf (stem, sizeof (stem), "%s", s && *s ? s : "texture");
		// keep the stem only: strip directories and the extension
		char *base = stem;
		for (char *p = stem; *p; p++)
			if (*p == '/' || *p == '\\')
				base = p + 1;
		char *dot = strrchr (base, '.');
		if (dot && dot != base)
			*dot = 0;
		snprintf (e->name, sizeof (e->name), "%s", base);
		// distinct textures with the same file name get a numeric suffix
		for (uint k = 0; k + 1 < n->n_tex; k++)
			if (!strcmp (n->tex[k].name, e->name))
			{
				snprintf (e->name + strlen (e->name), 12, "_%u", n->n_tex - 1);
				break;
			}
	}
}

uint NifNumTextures (const nif_t *n) { return n ? n->n_tex : 0; }

ccp NifTextureName (const nif_t *n, uint i) { return n && i < n->n_tex ? n->tex[i].name : 0; }

enumError NifDecodeTexture (const nif_t *n, uint i, u8 **rgba, uint *width, uint *height)
{
	psrc_t t;
	if (!n || i >= n->n_tex || !parse_psrc (n, n->tex[i].data_block, &t))
		return ERR_NOTHING_TO_DO;
	uint gx, need;
	if (t.fmt == 4)
	{
		gx = 14;
		need = ((t.w + 7) & ~7u) * ((t.h + 7) & ~7u) / 2;
	}
	else if (t.fmt == 1)
	{
		gx = 6;
		need = ((t.w + 3) & ~3u) * ((t.h + 3) & ~3u) * 4;
	}
	else
		return ERR_NOTHING_TO_DO;
	if (need > t.pix_size)
		return ERR_NOTHING_TO_DO;
	const enumError err = DecodeGXTexture_RGBA (rgba, t.w, t.h, gx, t.pix, need, 0, 0, 0);
	if (!err)
	{
		*width = t.w;
		*height = t.h;
	}
	return err;
}

static int nif_tex_by_block (const nif_t *n, u32 block)
{
	for (uint i = 0; i < n->n_tex; i++)
		if (n->tex[i].tex_block == block)
			return (int)i;
	return -1;
}

//-----------------------------------------------------------------------------
///////////////			NiDataStream			///////////////
//-----------------------------------------------------------------------------

#define MAX_COMP 8

typedef struct
{
	const u8 *data;
	uint nbytes, count, stride, ncomp;
	u32 fm[MAX_COMP];
} stream_t;

static bool parse_stream (const nif_t *n, uint b, stream_t *s)
{
	uint size;
	const u8 *d = blk (n, b, &size);
	if (!d || strcmp (nif_type (n, b), "NiDataStream"))
		return false;
	cur_t c = { d, d + size, true };
	memset (s, 0, sizeof (*s));
	s->nbytes = cu32 (&c);
	cu32 (&c);
	const uint nr = cu32 (&c);
	if (nr > 4096)
		return false;
	u64 count = 0;
	for (uint i = 0; i < nr; i++)
	{
		cu32 (&c);
		count += cu32 (&c);
	}
	s->ncomp = cu32 (&c);
	if (!c.ok || !s->ncomp || s->ncomp > MAX_COMP)
		return false;
	for (uint i = 0; i < s->ncomp; i++)
		s->fm[i] = cu32 (&c);
	if (!c.ok || (u64)(c.end - c.p) < s->nbytes || !count || count > s->nbytes)
		return false;
	s->data = c.p;
	s->count = (uint)count;
	s->stride = s->nbytes / s->count;
	return true;
}

// bytes per component and component count of a NiDataStream format word
static uint fm_ncomp (u32 f) { return (f >> 16) & 0xff; }

static uint fm_csize (u32 f)
{
	switch ((f >> 8) & 0xff)
	{
		case 4: return 4;
		case 2: return 2;
		case 1: return 1;
	}
	return 0;
}

// One semantic attribute: where to find element I of it.
typedef struct
{
	stream_t s;
	uint off, ncomp, csize;
	bool valid;
} attr_t;

static float attr_comp (const attr_t *a, uint elem, uint k)
{
	const u8 *p = a->s.data + (u64)elem * a->s.stride + a->off + k * a->csize;
	if (k >= a->ncomp)
		return 0;
	switch (a->csize)
	{
		case 4: return nbef (p);
		case 2: return (float)nbe16 (p) / 65535.0f;
		default: return *p / 255.0f;
	}
}

//-----------------------------------------------------------------------------
///////////////			transforms			///////////////
//-----------------------------------------------------------------------------

typedef struct
{
	float m[9];
	float t[3];
} xf_t; // v' = m * v + t

static xf_t xf_local (const nif_av_t *a)
{
	xf_t x;
	for (uint i = 0; i < 9; i++)
		x.m[i] = a->r[i] * a->s;
	memcpy (x.t, a->t, sizeof (x.t));
	return x;
}

static xf_t xf_mul (const xf_t *p, const xf_t *c)
{
	xf_t r;
	for (uint i = 0; i < 3; i++)
	{
		for (uint j = 0; j < 3; j++)
			r.m[i * 3 + j] = p->m[i * 3] * c->m[j] + p->m[i * 3 + 1] * c->m[3 + j]
				+ p->m[i * 3 + 2] * c->m[6 + j];
		r.t[i] = p->m[i * 3] * c->t[0] + p->m[i * 3 + 1] * c->t[1] + p->m[i * 3 + 2] * c->t[2]
			+ p->t[i];
	}
	return r;
}

static vec3_t xf_pt (const xf_t *x, const float v[3])
{
	// Gamebryo is Z-up; glTF is Y-up: (x, y, z) -> (x, z, -y)
	const float wx = x->m[0] * v[0] + x->m[1] * v[1] + x->m[2] * v[2] + x->t[0];
	const float wy = x->m[3] * v[0] + x->m[4] * v[1] + x->m[5] * v[2] + x->t[1];
	const float wz = x->m[6] * v[0] + x->m[7] * v[1] + x->m[8] * v[2] + x->t[2];
	vec3_t r = { wx, wz, -wy };
	return r;
}

static vec3_t xf_dir (const xf_t *x, const float v[3])
{
	const float wx = x->m[0] * v[0] + x->m[1] * v[1] + x->m[2] * v[2];
	const float wy = x->m[3] * v[0] + x->m[4] * v[1] + x->m[5] * v[2];
	const float wz = x->m[6] * v[0] + x->m[7] * v[1] + x->m[8] * v[2];
	const float l = sqrtf (wx * wx + wy * wy + wz * wz);
	vec3_t r = { wx, wz, -wy };
	if (l > 1e-12f)
	{
		r.x /= l;
		r.y /= l;
		r.z /= l;
	}
	return r;
}

//-----------------------------------------------------------------------------
///////////////			model building			///////////////
//-----------------------------------------------------------------------------

typedef struct
{
	const nif_t *n;
	ccp *png;
	model_t *model;
	int *mat_key; // per material: texture index * 2 + alpha
	uint n_mat;
	uint budget;
	bool hidden; // also export nodes flagged as hidden
} build_t;

static int build_material (build_t *b, int tex, bool alpha)
{
	const int key = (tex + 1) * 2 + (alpha ? 1 : 0);
	for (uint i = 0; i < b->n_mat; i++)
		if (b->mat_key[i] == key)
			return (int)i;
	model_t *m = b->model;
	material_t *nm = REALLOC (m->materials, (m->num_materials + 1) * sizeof (*nm));
	int *nk = REALLOC (b->mat_key, (b->n_mat + 1) * sizeof (*nk));
	if (!nm || !nk)
		return 0;
	m->materials = nm;
	b->mat_key = nk;
	material_t *mat = m->materials + m->num_materials;
	memset (mat, 0, sizeof (*mat));
	snprintf (mat->name, sizeof (mat->name), "Material_%u", (uint)m->num_materials);
	mat->diffuse[0] = mat->diffuse[1] = mat->diffuse[2] = mat->diffuse[3] = 1.0f;
	if (tex >= 0 && b->png && b->png[tex])
	{
		snprintf (mat->textures[0], sizeof (mat->textures[0]), "%s", b->png[tex]);
		mat->num_textures = 1;
		mat->wrap_s[0] = mat->wrap_t[0] = 1;
		mat->min_filter[0] = mat->mag_filter[0] = 1;
	}
	mat->has_alpha = alpha;
	b->mat_key[b->n_mat++] = key;
	return (int)m->num_materials++;
}

// Resolve the diffuse texture and alpha flag from a mesh's property list.
static void mesh_props (build_t *b, const nif_av_t *a, int *tex, bool *alpha)
{
	const nif_t *n = b->n;
	*tex = -1;
	*alpha = false;
	for (uint i = 0; i < a->n_props; i++)
	{
		const u32 p = a->props[i];
		ccp t = nif_type (n, p);
		uint size;
		const u8 *d = blk (n, p, &size);
		if (!d)
			continue;
		if (!strcmp (t, "NiAlphaProperty"))
		{
			// flags: bit0 alpha blend, bit9 alpha test
			cur_t c = { d, d + size, true };
			cu32 (&c);
			const uint ne = cu32 (&c);
			cskip (&c, 4ull * ne + 4);
			const uint fl = cu16 (&c);
			if (c.ok && (fl & 1))
				*alpha = true;
		}
		else if (!strcmp (t, "NiTexturingProperty"))
		{
			cur_t c = { d, d + size, true };
			cu32 (&c);
			const uint ne = cu32 (&c);
			cskip (&c, 4ull * ne + 4 + 2);
			cu32 (&c); // texture count
			if (cu8 (&c) && c.ok) // has base texture
			{
				const u32 src = cu32 (&c);
				if (c.ok)
					*tex = nif_tex_by_block (n, src);
			}
		}
	}
}

// Expand a GX display list to a triangle list of index tuples. Returns the
// number of corner tuples written to OUT (NATTR indices each), or -1.
static long dl_expand (const u8 *dl, uint size, uint nattr, u16 **out)
{
	const uint vs = 2 * nattr;
	size_t cap = 0, num = 0;
	u16 *soup = 0;
	uint p = 0;
	while (p < size && dl[p])
	{
		const uint op = dl[p] & 0xf8;
		if (p + 3 > size || (op != 0x90 && op != 0x98 && op != 0xa0 && op != 0x80))
		{
			FREE (soup);
			return -1;
		}
		const uint cnt = nbe16 (dl + p + 1);
		p += 3;
		if ((u64)p + (u64)cnt * vs > size)
		{
			FREE (soup);
			return -1;
		}
		const u8 *v = dl + p;
		p += cnt * vs;
		uint tri[4][3]; // corner indices of each emitted triangle
		uint ntri = 0;
		for (uint i = 0; i + (op == 0x80 ? 3 : 2) < cnt + (op == 0x80 ? 0 : 0) || i + 2 < cnt; i++)
		{
			ntri = 0;
			if (op == 0x90)
			{
				if (i % 3 || i + 2 >= cnt)
					continue;
				tri[ntri][0] = i, tri[ntri][1] = i + 1, tri[ntri++][2] = i + 2;
			}
			else if (op == 0x98)
			{
				if (i + 2 >= cnt)
					break;
				if (i & 1)
					tri[ntri][0] = i + 1, tri[ntri][1] = i, tri[ntri++][2] = i + 2;
				else
					tri[ntri][0] = i, tri[ntri][1] = i + 1, tri[ntri++][2] = i + 2;
			}
			else if (op == 0xa0)
			{
				if (i + 2 >= cnt)
					break;
				tri[ntri][0] = 0, tri[ntri][1] = i + 1, tri[ntri++][2] = i + 2;
			}
			else // quads: 4 vertices -> 2 triangles
			{
				if (i % 4 || i + 3 >= cnt)
					continue;
				tri[ntri][0] = i, tri[ntri][1] = i + 1, tri[ntri++][2] = i + 2;
				tri[ntri][0] = i, tri[ntri][1] = i + 2, tri[ntri++][2] = i + 3;
			}
			for (uint t = 0; t < ntri; t++)
			{
				if ((num + 3) * nattr > cap)
				{
					cap = cap ? cap * 2 : 3072;
					while ((num + 3) * nattr > cap)
						cap *= 2;
					u16 *ns = REALLOC (soup, cap * sizeof (*ns));
					if (!ns)
					{
						FREE (soup);
						return -1;
					}
					soup = ns;
				}
				for (uint k = 0; k < 3; k++)
					for (uint a = 0; a < nattr; a++)
						soup[(num + k) * nattr + a] = nbe16 (v + tri[t][k] * vs + a * 2);
				num += 3;
			}
		}
	}
	*out = soup;
	return (long)num;
}

typedef struct
{
	ccp name;
	uint index;
	attr_t a;
} sem_t;

static void set_v4 (color4_t *c, const attr_t *a, uint e)
{
	c->r = attr_comp (a, e, 0);
	c->g = attr_comp (a, e, 1);
	c->b = attr_comp (a, e, 2);
	c->a = a->ncomp > 3 ? attr_comp (a, e, 3) : 1.0f;
}

static bool build_mesh (build_t *b, uint blockno, const nif_av_t *av, const cur_t *after_av, const xf_t *xf)
{
	const nif_t *n = b->n;
	cur_t c = *after_av;
	const uint nmat = cu32 (&c);
	if (nmat > 64)
		return true;
	cskip (&c, 8ull * nmat + 4 + 1);
	cu32 (&c); // primitive type
	cu16 (&c); // submeshes
	cu8 (&c); // instancing
	cskip (&c, 16);
	const uint nstreams = cu32 (&c);
	if (!c.ok || nstreams > 32)
		return true;

	sem_t sems[24];
	uint nsem = 0;
	stream_t dl_stream, idx_stream;
	bool has_dl = false, has_idx = false;
	for (uint i = 0; i < nstreams && c.ok; i++)
	{
		const u32 ref = cu32 (&c);
		cu8 (&c);
		const uint nsub = cu16 (&c);
		cskip (&c, 2ull * nsub);
		const uint ncomp = cu32 (&c);
		if (!c.ok || ncomp > MAX_COMP)
			return true;
		stream_t s;
		const bool sok = parse_stream (n, ref, &s);
		uint off = 0;
		for (uint k = 0; k < ncomp; k++)
		{
			const u32 nm = cu32 (&c);
			const u32 ix = cu32 (&c);
			if (!c.ok || !sok || k >= s.ncomp)
				return true;
			ccp name = nif_string (n, nm);
			const uint csz = fm_csize (s.fm[k]), nc = fm_ncomp (s.fm[k]);
			if (name && !strcmp (name, "DISPLAYLIST"))
			{
				dl_stream = s;
				has_dl = true;
			}
			else if (name && !strcmp (name, "INDEX"))
			{
				idx_stream = s;
				has_idx = true;
			}
			else if (name && nsem < 24)
			{
				sems[nsem].name = name;
				sems[nsem].index = ix;
				sems[nsem].a.s = s;
				sems[nsem].a.off = off;
				sems[nsem].a.ncomp = nc;
				sems[nsem].a.csize = csz;
				sems[nsem].a.valid = csz && nc;
				nsem++;
			}
			off += nc * csz;
		}
	}
	if (!c.ok || (!has_dl && !has_idx))
		return true;

	// Semantic lookup. Bind-pose streams win over the runtime skinning output.
	const attr_t *pos = 0, *nrm = 0, *clr = 0, *uv0 = 0, *uv1 = 0;
	const attr_t *pos_bp = 0, *nrm_bp = 0;
	uint order[8], norder = 0; // display-list attributes in GX order
	for (uint i = 0; i < nsem; i++)
	{
		const sem_t *s = sems + i;
		if (!s->a.valid)
			continue;
		if (!strcmp (s->name, "POSITION") && !s->index)
			pos = &s->a, order[norder < 8 ? norder++ : 0] = 0;
		else if (!strcmp (s->name, "NORMAL") && !s->index)
			nrm = &s->a, order[norder < 8 ? norder++ : 0] = 1;
		else if (!strcmp (s->name, "COLOR") && !s->index)
			clr = &s->a, order[norder < 8 ? norder++ : 0] = 2;
		else if (!strcmp (s->name, "TEXCOORD") && s->index == 0)
			uv0 = &s->a, order[norder < 8 ? norder++ : 0] = 3;
		else if (!strcmp (s->name, "TEXCOORD") && s->index == 1)
			uv1 = &s->a, order[norder < 8 ? norder++ : 0] = 4;
		else if (!strcmp (s->name, "POSITION_BP"))
			pos_bp = &s->a;
		else if (!strcmp (s->name, "NORMAL_BP"))
			nrm_bp = &s->a;
	}
	if (pos_bp)
		pos = pos_bp;
	if (nrm_bp)
		nrm = nrm_bp;
	if (!pos || pos->ncomp < 3 || pos->csize != 4 || (uv0 && uv0->csize != 4))
		return true;

	// triangle corners: index tuples in `order` (display list) or one index
	u16 *soup = 0;
	long ncorner = 0;
	uint nattr = norder;
	if (has_dl)
	{
		ncorner = dl_expand (dl_stream.data, dl_stream.nbytes, nattr, &soup);
		if (ncorner <= 0)
			return true;
	}
	else
	{
		if (idx_stream.ncomp != 1 || fm_csize (idx_stream.fm[0]) != 2)
			return true;
		nattr = 1;
		ncorner = (long)(idx_stream.count / 3) * 3;
		soup = MALLOC ((ncorner ? ncorner : 1) * sizeof (*soup));
		if (!soup)
			return false;
		for (long i = 0; i < ncorner; i++)
			soup[i] = (u16)nbe16 (idx_stream.data + 2 * i);
	}

	model_t *m = b->model;
	mesh_t *nmesh = REALLOC (m->meshes, (m->num_meshes + 1) * sizeof (*nmesh));
	if (!nmesh)
	{
		FREE (soup);
		return false;
	}
	m->meshes = nmesh;
	mesh_t *mesh = m->meshes + m->num_meshes;
	memset (mesh, 0, sizeof (*mesh));
	ccp nm = nif_string (n, av->name);
	snprintf (mesh->name, sizeof (mesh->name), "%s", nm && *nm ? nm : "Mesh");
	int tex;
	bool alpha;
	mesh_props (b, av, &tex, &alpha);
	mesh->material_idx = build_material (b, tex, alpha);

	const uint np = pos->s.count;
	mesh->positions = CALLOC (np, sizeof (*mesh->positions));
	mesh->num_positions = np;
	mesh->vertices = CALLOC (ncorner, sizeof (*mesh->vertices));
	mesh->num_vertices = (size_t)ncorner;
	if (!mesh->positions || !mesh->vertices)
	{
		FREE (soup);
		FREE (mesh->positions);
		FREE (mesh->vertices);
		return false;
	}
	m->num_meshes++;

	for (uint i = 0; i < np; i++)
	{
		const float v[3] = { attr_comp (pos, i, 0), attr_comp (pos, i, 1), attr_comp (pos, i, 2) };
		mesh->positions[i] = xf_pt (xf, v);
	}
	if (nrm && nrm->ncomp >= 3 && nrm->csize == 4)
	{
		mesh->num_normals = nrm->s.count;
		mesh->normals = CALLOC (mesh->num_normals, sizeof (*mesh->normals));
		for (uint i = 0; mesh->normals && i < nrm->s.count; i++)
		{
			const float v[3] = { attr_comp (nrm, i, 0), attr_comp (nrm, i, 1), attr_comp (nrm, i, 2) };
			mesh->normals[i] = xf_dir (xf, v);
		}
		if (!mesh->normals)
			mesh->num_normals = 0;
	}
	if (clr && clr->ncomp >= 3)
	{
		mesh->num_colors[0] = clr->s.count;
		mesh->colors[0] = CALLOC (clr->s.count, sizeof (*mesh->colors[0]));
		for (uint i = 0; mesh->colors[0] && i < clr->s.count; i++)
			set_v4 (mesh->colors[0] + i, clr, i);
		if (!mesh->colors[0])
			mesh->num_colors[0] = 0;
	}
	if (uv0 && uv0->ncomp >= 2)
	{
		mesh->num_texcoords = uv0->s.count;
		mesh->texcoords = CALLOC (uv0->s.count, sizeof (*mesh->texcoords));
		for (uint i = 0; mesh->texcoords && i < uv0->s.count; i++)
		{
			mesh->texcoords[i].u = attr_comp (uv0, i, 0);
			mesh->texcoords[i].v = attr_comp (uv0, i, 1);
		}
		if (!mesh->texcoords)
			mesh->num_texcoords = 0;
	}
	if (uv1 && uv1->ncomp >= 2 && uv1->csize == 4)
	{
		mesh->num_extra_texcoords[0] = uv1->s.count;
		mesh->extra_texcoords[0] = CALLOC (uv1->s.count, sizeof (*mesh->extra_texcoords[0]));
		for (uint i = 0; mesh->extra_texcoords[0] && i < uv1->s.count; i++)
		{
			mesh->extra_texcoords[0][i].u = attr_comp (uv1, i, 0);
			mesh->extra_texcoords[0][i].v = attr_comp (uv1, i, 1);
		}
		if (!mesh->extra_texcoords[0])
			mesh->num_extra_texcoords[0] = 0;
	}

	// The DL indexes each attribute's own stream. A skinned mesh's DL indexes
	// POSITION/NORMAL of the runtime streams, which have the same element
	// counts as their _BP twins.
	for (long i = 0; i < ncorner; i++)
	{
		vertex_t *v = mesh->vertices + i;
		v->position_idx = v->normal_idx = v->tangent_idx = v->texcoord_idx = v->matrix_idx = -1;
		v->color_idx[0] = v->color_idx[1] = -1;
		for (int e = 0; e < 7; e++)
			v->extra_texcoord_idx[e] = -1;
		if (!has_dl)
		{
			const int ix = soup[i];
			v->position_idx = ix;
			v->normal_idx = mesh->num_normals ? ix : -1;
			v->color_idx[0] = mesh->num_colors[0] ? ix : -1;
			v->texcoord_idx = mesh->num_texcoords ? ix : -1;
			if (mesh->num_extra_texcoords[0])
				v->extra_texcoord_idx[0] = ix;
			continue;
		}
		for (uint a = 0; a < nattr; a++)
		{
			const int ix = soup[i * nattr + a];
			switch (order[a])
			{
				case 0: v->position_idx = ix; break;
				case 1: v->normal_idx = mesh->num_normals ? ix : -1; break;
				case 2: v->color_idx[0] = mesh->num_colors[0] ? ix : -1; break;
				case 3: v->texcoord_idx = mesh->num_texcoords ? ix : -1; break;
				default: v->extra_texcoord_idx[0] = mesh->num_extra_texcoords[0] ? ix : -1;
			}
		}
	}
	FREE (soup);
	// drop meshes whose indices point outside their arrays
	for (size_t i = 0; i < mesh->num_vertices; i++)
	{
		const vertex_t *v = mesh->vertices + i;
		if ((size_t)v->position_idx >= mesh->num_positions
			|| (v->normal_idx >= 0 && (size_t)v->normal_idx >= mesh->num_normals)
			|| (v->color_idx[0] >= 0 && (size_t)v->color_idx[0] >= mesh->num_colors[0])
			|| (v->texcoord_idx >= 0 && (size_t)v->texcoord_idx >= mesh->num_texcoords)
			|| (v->extra_texcoord_idx[0] >= 0
				&& (size_t)v->extra_texcoord_idx[0] >= mesh->num_extra_texcoords[0]))
		{
			m->num_meshes--;
			FREE (mesh->positions);
			FREE (mesh->normals);
			FREE (mesh->colors[0]);
			FREE (mesh->texcoords);
			FREE (mesh->extra_texcoords[0]);
			FREE (mesh->vertices);
			return true;
		}
	}
	(void)blockno;
	return true;
}

static bool is_node_type (ccp t)
{
	return !strcmp (t, "NiNode") || !strcmp (t, "NiBillboardNode") || !strcmp (t, "NiLODNode")
		|| !strcmp (t, "NiSwitchNode") || !strcmp (t, "NiSortAdjustNode");
}

static bool walk (build_t *b, uint blockno, const xf_t *parent, uint depth)
{
	const nif_t *n = b->n;
	if (blockno >= n->n_blocks || depth > NIF_MAX_DEPTH || !b->budget)
		return true;
	b->budget--;
	ccp t = nif_type (n, blockno);
	const bool node = is_node_type (t);
	if (!node && strcmp (t, "NiMesh"))
		return true;
	uint size;
	const u8 *d = blk (n, blockno, &size);
	cur_t c = { d, d + size, true };
	nif_av_t av;
	if (!read_av (&c, &av) || ((av.flags & 1) && !b->hidden))
		return true; // hidden helpers (collision, fx) stay out unless nothing else exists
	const xf_t local = xf_local (&av);
	const xf_t world = xf_mul (parent, &local);
	if (!node)
		return build_mesh (b, blockno, &av, &c, &world);

	const uint nc = cu32 (&c);
	if (!c.ok || nc > 4096)
		return true;
	// LOD / switch nodes list alternatives: only the first one is exported
	const bool one = !strcmp (t, "NiLODNode") || !strcmp (t, "NiSwitchNode");
	for (uint i = 0; i < nc && c.ok; i++)
	{
		const u32 child = cu32 (&c);
		if (c.ok && child != NIF_NONE && !walk (b, child, &world, depth + 1))
			return false;
		if (one)
			break;
	}
	return true;
}

model_t *NifBuildModel (const nif_t *n, ccp *png_names)
{
	if (!n)
		return 0;
	model_t *model = CALLOC (1, sizeof (*model));
	if (!model)
		return 0;
	build_t b = { n, png_names, model, 0, 0, 100000, false };
	xf_t ident = { { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { 0, 0, 0 } };
	bool ok = true;
	for (uint pass = 0; pass < 2 && ok && !model->num_meshes; pass++)
	{
		b.hidden = pass == 1;
		b.budget = 100000;
		for (uint i = 0; ok && i < n->n_roots; i++)
			ok = walk (&b, nbe32 (n->d + n->roots_off + 4 + 4 * i), &ident, 0);
	}
	FREE (b.mat_key);
	if (!ok || !model->num_meshes)
	{
		FreeModel (model);
		return 0;
	}
	return model;
}
