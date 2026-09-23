// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// BombShell engine data pack (.xwi / .xdx9) scanner; see lib-bombshell.h.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-bombshell.h"
#include "lib-bntx.h"
#include "lib-excite.h"
#include <math.h>

#define BS_MAGIC0 0x020100a0u
#define BS_MAGIC1 0x040100afu
#define BS_MAX_DIRS 16
#define BS_MAX_ASSETS 20000
#define BS_MAX_LOG2 12

typedef struct bs_ctx_t
{
	const u8 *d;
	size_t size;
	bool be;
} bs_ctx_t;

static u32 bs_u32 (const bs_ctx_t *c, size_t o)
{
	if (!c || c->size < 4 || o > c->size - 4)
		return 0;
	const u8 *p = c->d + o;
	return c->be ? (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3] : (u32)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0];
}

static bool bs_init (bs_ctx_t *c, const u8 *d, size_t size)
{
	c->d = d;
	c->size = size;
	if (size < 16 + 24 || size > 0xffffffffu)
		return false;
	c->be = true;
	if (bs_u32 (c, 0) == BS_MAGIC0 && bs_u32 (c, 4) == BS_MAGIC1)
		return true;
	c->be = false;
	return bs_u32 (c, 0) == BS_MAGIC0 && bs_u32 (c, 4) == BS_MAGIC1;
}

bool IsBombshellPack (const u8 *d, size_t size)
{
	bs_ctx_t c;
	if (!bs_init (&c, d, size))
		return false;
	const u32 n = bs_u32 (&c, 12);
	return n && n <= BS_MAX_DIRS && 16 + 24ull * n <= size;
}

static float bs_f32 (const u8 *p)
{
	const u32 v = (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static u32 bs_align (const bs_ctx_t *c, u32 v)
{
	return c->be ? (v + 31) & ~31u : v;
}

// File-system safe base name: the part after the last path separator, without
// a short extension.
static void bs_clean_name (char *out, size_t out_size, const u8 *src, size_t max, uint fallback)
{
	char raw[256];
	size_t n = 0;
	while (n < max && n + 1 < sizeof (raw) && src[n])
		raw[n] = src[n], n++;
	raw[n] = 0;

	ccp base = raw;
	for (ccp p = raw; *p; p++)
		if (*p == '\\' || *p == '/')
			base = p + 1;
	char tmp[256];
	snprintf (tmp, sizeof (tmp), "%s", base);
	char *dot = strrchr (tmp, '.');
	if (dot && dot != tmp && strlen (dot) <= 5)
		*dot = 0;
	size_t o = 0;
	for (ccp p = tmp; *p && o + 1 < out_size; p++)
	{
		const u8 ch = *p;
		out[o++] = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
			|| ch == '_' || ch == '-' || ch == '.' || ch == ' ' || ch == '+' || ch == '(' || ch == ')'
			? ch : '_';
	}
	out[o] = 0;
	if (!*out || !strcmp (out, ".") || !strcmp (out, ".."))
		snprintf (out, out_size, "unnamed_%04u", fallback);
}

// Make the name unique among the assets of the same kind and directory (the
// file system is case-insensitive).
// Rename list[n] to "<name>_<k>" if an earlier asset of the same kind and
// directory already uses its name. One pass finds the highest suffix in use:
// retrying _2, _3, ... with a rescan each was cubic in the asset count, which
// a corrupt pack repeating one record BS_MAX_ASSETS times turns into a hang.
static void bs_unique (bombshell_asset_t *list, uint n)
{
	bombshell_asset_t *a = list + n;
	char base[sizeof (a->name)];
	snprintf (base, sizeof (base), "%s", a->name);
	const size_t tlen = strnlen (base, 80); // suffixed names keep %.80s of it
	bool clash = false;
	uint max_k = 1;
	for (uint i = 0; i < n; i++)
	{
		const bombshell_asset_t *o = list + i;
		if (o->kind != a->kind || o->dir != a->dir)
			continue;
		if (!strcasecmp (o->name, base))
			clash = true;
		else if (!strncasecmp (o->name, base, tlen) && o->name[tlen] == '_'
			&& isdigit ((uchar)o->name[tlen + 1]))
		{
			char *end;
			const unsigned long k = strtoul (o->name + tlen + 1, &end, 10);
			if (!*end && k >= max_k && k < UINT_MAX)
				max_k = (uint)k;
		}
	}
	if (clash)
		snprintf (a->name, sizeof (a->name), "%.80s_%u", base, max_k + 1);
}

static u32 bs_tex_bytes (const bs_ctx_t *c, uint fmt, uint w, uint h)
{
	if (c->be)
		return fmt == 0x45 || fmt == 0xc5 || fmt == 0xc6 ? (u32)(w * h / 2) : 0;
	switch (fmt)
	{
		case 0x45: return w * h / 2;
		case 0xca: return w * h;
		case 0xa0: return w * h * 4;
		case 0x18: return w * h * 3;
	}
	return 0;
}

bombshell_asset_t *ListBombshell (const u8 *d, size_t size, uint *count)
{
	*count = 0;
	bs_ctx_t c;
	if (!IsBombshellPack (d, size) || !bs_init (&c, d, size))
		return 0;

	const u32 ndir = bs_u32 (&c, 12);
	const size_t glob = 16 + 24ull * ndir;
	const u32 hdr = c.be ? 192 : 188;
	bombshell_asset_t *list = CALLOC (BS_MAX_ASSETS, sizeof (*list));
	uint n = 0;

	for (uint k = 0; k < ndir; k++)
	{
		const size_t o = 16 + 24ull * k;
		const uint type = bs_u32 (&c, o + 4);
		const size_t dp = glob + bs_u32 (&c, o + 20);
		if (dp + hdr > size)
			continue;

		const u32 sound_bytes = bs_u32 (&c, dp + 4);
		const u32 table_words = bs_u32 (&c, dp + 8);
		const u32 n_tex = bs_u32 (&c, dp + 20);
		const u32 n_snd = bs_u32 (&c, dp + 32);
		const u32 n_list = bs_u32 (&c, dp + 108);
		const size_t table = dp + hdr;
		size_t e1 = table, end = table;
		if (sound_bytes)
		{
			e1 = table + bs_align (&c, table_words * 4);
			end = bs_align (&c, e1 + sound_bytes);
		}
		if (n_list)
			end += bs_align (&c, n_list * 4);
		const size_t E = end;

		for (uint i = 0; sound_bytes && i < n_snd && n < BS_MAX_ASSETS; i++)
		{
			const size_t rec = e1 + bs_u32 (&c, table + 8ull * i);
			const u64 off = e1 + (u64)bs_u32 (&c, rec), sz = bs_u32 (&c, rec + 4);
			if (rec + 16 > size || !sz || off + sz > size)
				continue;
			bombshell_asset_t *a = list + n;
			a->kind = BSA_SOUND;
			a->dir = k;
			a->dir_type = type;
			a->big_endian = c.be;
			a->off = off;
			a->size = sz;
			const u8 *magic = d + off;
			snprintf (a->ext, sizeof (a->ext), "%s", sz >= 4 && !memcmp (magic, "FSB", 3) ? "fsb"
				: sz >= 4 && !memcmp (magic, "RIFF", 4) ? "wav" : "bin");
			const size_t nm = off + sz;
			bs_clean_name (a->name, sizeof (a->name), d + nm, nm < size ? size - nm : 0, i);
			bs_unique (list, n);
			n++;
		}

		for (uint i = 0; i < n_tex && n < BS_MAX_ASSETS; i++)
		{
			const size_t r = E + bs_u32 (&c, E + 4ull * i);
			if (E + 4ull * i + 4 > size || r + 24 > size)
				continue;
			const uint fmt = d[r + 4], lw = d[r + 6], lh = d[r + 7];
			if (lw > BS_MAX_LOG2 || lh > BS_MAX_LOG2)
				continue;
			const uint w = 1u << lw, h = 1u << lh;
			const u32 bytes = bs_tex_bytes (&c, fmt, w, h);
			const u64 pix = E + (u64)bs_u32 (&c, r + 8);
			if (pix + bytes > size)
				continue;
			bombshell_asset_t *a = list + n;
			a->kind = BSA_TEXTURE;
			a->dir = k;
			a->dir_type = type;
			a->big_endian = c.be;
			a->width = w;
			a->height = h;
			a->format = fmt;
			a->index = i;
			a->off = pix;
			a->size = bytes;
			const u32 alpha = bs_u32 (&c, r + 20), aux = bs_u32 (&c, r + 12);
			if (alpha && E + (u64)alpha + bytes <= size)
				a->off_alpha = E + alpha;
			if (aux && E + (u64)aux < size)
				a->off_aux = E + aux;
			const size_t nm = E + bs_u32 (&c, r + 16);
			bs_clean_name (a->name, sizeof (a->name), d + nm, nm < size ? size - nm : 0, i);
			bs_unique (list, n);
			n++;
		}
	}
	if (!n)
	{
		FREE (list);
		return 0;
	}
	*count = n;
	return list;
}

static void bs_bc_image (u8 *rgba, uint w, uint h, const u8 *src, uint block_bytes, bool dxt5)
{
	for (uint by = 0; by < h; by += 4)
		for (uint bx = 0; bx < w; bx += 4)
		{
			u8 px[64];
			if (dxt5)
				decode_bc3_block (src, px);
			else
				decode_bc1_block (src, px, true);
			src += block_bytes;
			for (uint y = 0; y < 4 && by + y < h; y++)
				for (uint x = 0; x < 4 && bx + x < w; x++)
					memcpy (rgba + 4 * ((size_t)(by + y) * w + bx + x), px + 4 * (y * 4 + x), 4);
		}
}

enumError DecodeBombshellTexture (u8 **rgba, const u8 *d, size_t size, const bombshell_asset_t *a)
{
	if (a->kind != BSA_TEXTURE || !a->size || a->off + (u64)a->size > size)
		return ERR_NOTHING_TO_DO;
	const uint w = a->width, h = a->height;
	if (a->big_endian)
	{
		u8 *img = 0;
		enumError err = DecodeGXTexture_RGBA (&img, w, h, 14, d + a->off, a->size, 0, 0, 0);
		if (err)
			return err;
		if (a->off_alpha)
		{
			u8 *alpha = 0;
			if (!DecodeGXTexture_RGBA (&alpha, w, h, 14, d + a->off_alpha, a->size, 0, 0, 0))
			{
				for (size_t i = 0; i < (size_t)w * h; i++)
					img[4 * i + 3] = alpha[4 * i];
				FREE (alpha);
			}
		}
		*rgba = img;
		return ERR_OK;
	}

	u8 *img = MALLOC ((size_t)w * h * 4);
	if (!img)
		return ERR_OUT_OF_MEMORY;
	const u8 *src = d + a->off;
	switch (a->format)
	{
		case 0x45:
			bs_bc_image (img, w, h, src, 8, false);
			break;
		case 0xca:
			bs_bc_image (img, w, h, src, 16, true);
			break;
		case 0xa0:
			for (size_t i = 0; i < (size_t)w * h; i++)
			{
				img[4 * i] = src[4 * i + 2];
				img[4 * i + 1] = src[4 * i + 1];
				img[4 * i + 2] = src[4 * i];
				img[4 * i + 3] = src[4 * i + 3];
			}
			break;
		case 0x18:
			for (size_t i = 0; i < (size_t)w * h; i++)
			{
				img[4 * i] = src[3 * i + 2];
				img[4 * i + 1] = src[3 * i + 1];
				img[4 * i + 2] = src[3 * i];
				img[4 * i + 3] = 255;
			}
			break;
		default:
			FREE (img);
			return ERR_NOTHING_TO_DO;
	}
	*rgba = img;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// Wii models
//-----------------------------------------------------------------------------

#define BS_MAX_MODELS 4096

// Absolute offsets of the model records of a big-endian pack (with the pointer
// bias M of their datapack). Returns the count.
static uint bs_models (const bs_ctx_t *c, u32 *rec, u32 *bias, u32 *dps, uint max)
{
	if (!c->be)
		return 0;
	const u32 ndir = bs_u32 (c, 12);
	const size_t glob = 16 + 24ull * ndir;
	uint n = 0;
	for (uint k = 0; k < ndir; k++)
	{
		const size_t o = 16 + 24ull * k;
		const size_t dp = glob + bs_u32 (c, o + 20);
		const u64 end = dp + (u64)bs_u32 (c, o + 16);
		const u32 nm = bs_u32 (c, dp + 56), words = bs_u32 (c, dp + 12);
		if (!nm || nm > BS_MAX_MODELS || dp + 192 > c->size || end > c->size)
			continue;
		const s64 list = (s64)end - ((s64)words * 4 + (s64)nm * 8);
		const s64 m = list - (s64)bs_u32 (c, dp);
		if (list < 0 || m < 0 || list + 8 * (s64)nm > (s64)c->size)
			continue;
		for (uint i = 0; i < nm && n < max; i++)
		{
			const u32 ptr = bs_u32 (c, (size_t)list + 8 * i);
			if ((u64)ptr + m + 4 > c->size)
				continue;
			const u64 r = (u64)bs_u32 (c, (size_t)(ptr + m)) + m;
			const u64 name = (u64)bs_u32 (c, (size_t)r + 4) + m;
			if (r + 0x20 > c->size || name + 8 > c->size || memcmp (c->d + name, "Content", 7))
				continue;
			bool dup = false;
			for (uint j = 0; j < n && !dup; j++)
				dup = rec[j] == r;
			if (dup)
				continue;
			rec[n] = (u32)r;
			dps[n] = (u32)dp;
			bias[n++] = (u32)m;
		}
	}
	return n;
}

uint CountBombshellModels (const u8 *d, size_t size)
{
	bs_ctx_t c;
	if (!bs_init (&c, d, size))
		return 0;
	u32 *rec = MALLOC (BS_MAX_MODELS * 12);
	const uint n = rec ? bs_models (&c, rec, rec + BS_MAX_MODELS, rec + 2 * BS_MAX_MODELS, BS_MAX_MODELS) : 0;
	FREE (rec);
	return n;
}

static void bs_tri (uint (*out)[3], uint *n, uint a, uint b, uint c)
{
	out[*n][0] = a;
	out[*n][1] = b;
	out[*n][2] = c;
	(*n)++;
}

// Builds one mesh from a sub-mesh record R; false when it is not decodable.
static bool bs_submesh (model_t *m, const bs_ctx_t *c, u32 r, u32 bias, uint index, u32 dp,
	const bombshell_asset_t *assets, uint n_assets)
{
	const u8 *d = c->d;
	const size_t size = c->size;
	const u32 type = bs_u32 (c, r);
	if ((type != 0 && type != 1) || (u64)r + 0xe0 > size)
		return false;
	const u32 flags = bs_u32 (c, r + 0xb0) & 0xf;
	if (!(flags & 1))
		return false;
	const u64 pos = (u64)bs_u32 (c, r + 180), nrm = (u64)bs_u32 (c, r + 192), uv = (u64)bs_u32 (c, r + 196),
		col = (u64)bs_u32 (c, r + (type ? 184 : 188)), dlh = (u64)bs_u32 (c, r + 200);
	if (!pos || !dlh || dlh + bias + 12 > size)
		return false;
	const u64 dl_bytes = bs_u32 (c, (size_t)(dlh + bias) + 4), dl = (u64)bs_u32 (c, (size_t)(dlh + bias) + 8) + bias;
	if (!dl_bytes || dl + dl_bytes > size)
		return false;
	uint per = 0;
	for (uint b = 0; b < 4; b++)
		per += (flags >> b & 1) * 2;

	// pass 1: count vertices and triangles, and the largest indices
	uint maxi[4] = { 0, 0, 0, 0 };
	size_t nv = 0, nt = 0;
	for (int pass = 0; pass < 2; pass++)
	{
		size_t p = 0;
		nv = 0;
		nt = 0;
		while (p + 3 <= dl_bytes)
		{
			const u8 cmd = d[dl + p];
			if (!cmd)
			{
				p++;
				continue;
			}
			const uint n = d[dl + p + 1] << 8 | d[dl + p + 2];
			p += 3;
			const uint kind = cmd & 0xf8;
			if ((kind != 0x80 && kind != 0x90 && kind != 0x98 && kind != 0xa0) || p + (size_t)n * per > dl_bytes)
				return false;
			for (uint i = 0; i < n; i++)
			{
				const u8 *q = d + dl + p + (size_t)i * per;
				uint b = 0;
				for (uint a = 0; a < 4; a++)
					if (flags >> a & 1)
					{
						const uint v = q[b] << 8 | q[b + 1];
						b += 2;
						if (v > maxi[a])
							maxi[a] = v;
					}
			}
			nv += n;
			nt += kind == 0x90 ? n / 3 : kind == 0x80 ? n / 4 * 2 : n >= 2 ? n - 2 : 0;
			p += (size_t)n * per;
		}
		break;
	}
	if (!nt || nv > 400000)
		return false;
	const u8 *arr[4] = { d + pos, d + nrm, d + col, d + uv };
	const uint width[4] = { 12, 12, 4, 4 };
	const u64 base[4] = { pos, nrm, col, uv };
	for (uint a = 0; a < 4; a++)
		if ((flags >> a & 1) && (!base[a] || base[a] + bias + (u64)(maxi[a] + 1) * width[a] > size))
			return false;
	(void)arr;

	mesh_t *nmesh = REALLOC (m->meshes, (m->num_meshes + 1) * sizeof (*nmesh));
	if (!nmesh)
		return false;
	m->meshes = nmesh;
	mesh_t *mesh = m->meshes + m->num_meshes;
	memset (mesh, 0, sizeof (*mesh));
	snprintf (mesh->name, sizeof (mesh->name), "part%u", index);
	const size_t cnt = nt * 3;
	mesh->positions = CALLOC (cnt, sizeof (vec3_t));
	mesh->normals = CALLOC (cnt, sizeof (vec3_t));
	mesh->texcoords = CALLOC (cnt, sizeof (vec2_t));
	mesh->vertices = CALLOC (cnt, sizeof (vertex_t));
	uint (*tri)[3] = CALLOC (nt, sizeof (*tri));
	uint *vidx = CALLOC (nv, 4 * sizeof (uint));
	if (!mesh->positions || !mesh->normals || !mesh->texcoords || !mesh->vertices || !tri || !vidx)
	{
		FREE (mesh->positions);
		FREE (mesh->normals);
		FREE (mesh->texcoords);
		FREE (mesh->vertices);
		FREE (tri);
		FREE (vidx);
		return false;
	}
	m->num_meshes++;
	// pass 2: gather vertex index tuples and triangulate
	size_t p = 0, v0 = 0;
	uint ntri = 0;
	while (p + 3 <= dl_bytes)
	{
		const u8 cmd = d[dl + p];
		if (!cmd)
		{
			p++;
			continue;
		}
		const uint n = d[dl + p + 1] << 8 | d[dl + p + 2];
		p += 3;
		const uint kind = cmd & 0xf8;
		for (uint i = 0; i < n; i++)
		{
			const u8 *q = d + dl + p + (size_t)i * per;
			uint b = 0;
			for (uint a = 0; a < 4; a++)
				vidx[(v0 + i) * 4 + a] = flags >> a & 1 ? (b += 2, q[b - 2] << 8 | q[b - 1]) : 0;
		}
		if (kind == 0x90)
			for (uint i = 0; i + 2 < n; i += 3)
				bs_tri (tri, &ntri, v0 + i, v0 + i + 1, v0 + i + 2);
		else if (kind == 0x98)
			for (uint i = 2; i < n; i++)
			{
				if (i & 1)
					bs_tri (tri, &ntri, v0 + i - 1, v0 + i - 2, v0 + i);
				else
					bs_tri (tri, &ntri, v0 + i - 2, v0 + i - 1, v0 + i);
			}
		else if (kind == 0xa0)
			for (uint i = 2; i < n; i++)
				bs_tri (tri, &ntri, v0, v0 + i, v0 + i - 1);
		else
			for (uint i = 0; i + 3 < n; i += 4)
			{
				bs_tri (tri, &ntri, v0 + i, v0 + i + 1, v0 + i + 2);
				bs_tri (tri, &ntri, v0 + i, v0 + i + 2, v0 + i + 3);
			}
		v0 += n;
		p += (size_t)n * per;
	}
	size_t nout = 0;
	for (uint t = 0; t < ntri && nout + 3 <= cnt; t++)
	{
		// strips are joined with repeated vertices: skip those degenerate triangles
		const uint *v0i = vidx + 4 * (size_t)tri[t][0], *v1i = vidx + 4 * (size_t)tri[t][1],
			*v2i = vidx + 4 * (size_t)tri[t][2];
		if (v0i[0] == v1i[0] || v1i[0] == v2i[0] || v0i[0] == v2i[0])
			continue;
		for (uint k = 0; k < 3; k++)
		{
			const size_t o = nout + k;
			const uint *ix = vidx + 4 * (size_t)tri[t][k];
			const u8 *pp = d + pos + bias + 12 * (size_t)ix[0];
			mesh->positions[o] = (vec3_t){ bs_f32 (pp), bs_f32 (pp + 4), bs_f32 (pp + 8) };
			if (flags & 2)
			{
				const u8 *np_ = d + nrm + bias + 12 * (size_t)ix[1];
				mesh->normals[o] = (vec3_t){ bs_f32 (np_), bs_f32 (np_ + 4), bs_f32 (np_ + 8) };
			}
			else
				mesh->normals[o] = (vec3_t){ 0, 0, 1 };
			if (flags & 8)
			{
				const u8 *tp = d + uv + bias + 4 * (size_t)ix[3];
				mesh->texcoords[o] = (vec2_t){ (int16_t)(tp[0] << 8 | tp[1]) / 256.0f, (int16_t)(tp[2] << 8 | tp[3]) / 256.0f };
			}
			vertex_t *vt = mesh->vertices + o;
			vt->position_idx = vt->normal_idx = vt->texcoord_idx = (int)o;
			vt->tangent_idx = vt->matrix_idx = -1;
			vt->color_idx[0] = vt->color_idx[1] = -1;
			for (int e = 0; e < 7; e++)
				vt->extra_texcoord_idx[e] = -1;
		}
		nout += 3;
	}
	mesh->num_positions = mesh->num_normals = mesh->num_texcoords = mesh->num_vertices = nout;
	FREE (tri);
	FREE (vidx);

	// texture: the patch list entry whose pointer is the sub-mesh's +48 / +56 field
	char texname[96] = "";
	uint dir = 0;
	for (uint k = 0; k < bs_u32 (c, 12); k++)
		if (16 + 24ull * bs_u32 (c, 12) + bs_u32 (c, 16 + 24 * (size_t)k + 20) == dp)
			dir = k;
	const u32 ntp = bs_u32 (c, dp + 16);
	const s64 ip = (s64)bias - 8 * ((s64)ntp + bs_u32 (c, dp + 52) + bs_u32 (c, dp + 28) + bs_u32 (c, dp + 84));
	for (uint f = 0; f < 2 && !*texname && ip >= 0; f++)
		for (uint i = 0; i < ntp && ip + 8 * (s64)i + 8 <= (s64)size; i++)
			if (bs_u32 (c, (size_t)ip + 8 * i) + (u64)bias == (u64)r + (f ? 56 : 48))
			{
				const uint ti = d[ip + 8 * i + 6] << 8 | d[ip + 8 * i + 7];
				for (uint a = 0; a < n_assets; a++)
					if (assets[a].kind == BSA_TEXTURE && assets[a].index == ti && assets[a].dir == dir)
						snprintf (texname, sizeof (texname), "%s", assets[a].name);
				break;
			}
	material_t *nmat = REALLOC (m->materials, (m->num_materials + 1) * sizeof (*m->materials));
	if (nmat)
	{
		m->materials = nmat;
		material_t *mt = m->materials + m->num_materials;
		memset (mt, 0, sizeof (*mt));
		mt->diffuse[0] = mt->diffuse[1] = mt->diffuse[2] = mt->diffuse[3] = 1.0f;
		snprintf (mt->name, sizeof (mt->name), "part%u", index);
		if (*texname && (flags & 8))
		{
			snprintf (mt->textures[0], sizeof (mt->textures[0]), "../textures/%s", texname);
			mt->num_textures = 1;
			mt->wrap_s[0] = mt->wrap_t[0] = 1;
			mt->min_filter[0] = mt->mag_filter[0] = 1;
			mt->has_alpha = 1;
		}
		mesh->material_idx = (int)m->num_materials++;
	}
	return true;
}

model_t *BuildBombshellModel (const u8 *d, size_t size, uint index, char *name, size_t name_size,
	const bombshell_asset_t *assets, uint n_assets, uint *dir_out, uint *type_out)
{
	bs_ctx_t c;
	if (!bs_init (&c, d, size))
		return 0;
	u32 *rec = MALLOC (BS_MAX_MODELS * 12);
	if (!rec)
		return 0;
	u32 *bias = rec + BS_MAX_MODELS, *dps = rec + 2 * BS_MAX_MODELS;
	const uint n = bs_models (&c, rec, bias, dps, BS_MAX_MODELS);
	if (index >= n)
	{
		FREE (rec);
		return 0;
	}
	const u32 r = rec[index], b = bias[index], dp = dps[index];
	FREE (rec);
	for (uint k = 0; k < bs_u32 (&c, 12); k++)
		if (16 + 24ull * bs_u32 (&c, 12) + bs_u32 (&c, 16 + 24 * (size_t)k + 20) == dp)
		{
			if (dir_out)
				*dir_out = k;
			if (type_out)
				*type_out = bs_u32 (&c, 16 + 24 * (size_t)k + 4);
		}
	if (name)
	{
		const size_t no = (size_t)bs_u32 (&c, r + 4) + b;
		char raw[160];
		ccp src_p = no < size ? (ccp)d + no : "";
		int src_len = no < size ? (int)(size - no < 159 ? size - no : 159) : 0;
		snprintf (raw, sizeof (raw), "%.*s", src_len, src_p);
		bs_clean_name (name, name_size, (const u8 *)raw, strlen (raw), index);
	}
	model_t *m = CALLOC (1, sizeof (*m));
	if (!m)
		return 0;
	const u32 nl = bs_u32 (&c, r + 16);
	const u64 lists = (u64)bs_u32 (&c, r + 20) + b;
	if (nl && lists + 8 <= size)
	{
		const u32 cnt = bs_u32 (&c, (size_t)lists);
		const u64 arr = (u64)bs_u32 (&c, (size_t)lists + 4) + b;
		for (uint i = 0; i < cnt && i < 256 && arr + 4ull * (i + 1) <= size; i++)
		{
			const u32 sub = bs_u32 (&c, (size_t)arr + 4 * i);
			if (sub)
				bs_submesh (m, &c, sub + b, b, i, dp, assets, n_assets);
		}
	}
	if (!m->num_meshes)
	{
		FreeModel (m);
		return 0;
	}
	return m;
}
