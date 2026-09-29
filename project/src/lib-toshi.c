// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Toshi TSFB containers and texture libraries; see lib-toshi.h.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-toshi.h"
#include "lib-excite.h"
#include <string.h>

#define TOSHI_MAX_SECTION (512u << 20)

static u32 ts_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
static u32 ts_be16 (const u8 *p)
{
	return p[0] << 8 | p[1];
}

bool IsToshiTsfb (const u8 *d, size_t size)
{
	return size >= 32 && !memcmp (d, "TSFB", 4) && !memcmp (d + 8, "FBRT", 4);
}

static enumError ts_btec (u8 **out, size_t *out_size, const u8 *b, size_t n)
{
	if (n < 16 || memcmp (b, "CETB", 4))
		return ERR_INVALID_DATA;
	const uint major = ts_be16 (b + 4), minor = ts_be16 (b + 6);
	const u32 csize = ts_be32 (b + 8), size = ts_be32 (b + 12);
	size_t p = 16;
	u32 xor = 0;
	if (major != 1 || (minor != 2 && minor != 3) || size > TOSHI_MAX_SECTION)
		return ERR_INVALID_DATA;
	if (minor == 3)
	{
		if (n < 20)
			return ERR_INVALID_DATA;
		xor= ts_be32 (b + 16);
		p = 20;
	}
	u8 *o = MALLOC (size ? size : 1);
	if (!o)
		return ERR_OUT_OF_MEMORY;
	size_t op = 0;
	s64 left = csize;
	while (left > 0)
	{
		if (p >= n)
			goto bad;
		const uint c = b[p++];
		uint used = 1, sz = c & 0x3f;
		if (c & 0x40)
		{
			if (p >= n)
				goto bad;
			sz = sz << 8 | b[p++];
			used++;
		}
		uint off = 0;
		if (!(c & 0x80))
		{
			if (p >= n)
				goto bad;
			off = b[p++];
			used++;
			if (off & 0x80)
			{
				if (p >= n)
					goto bad;
				off = (off & 0x7f) << 8 | b[p++];
				used++;
			}
		}
		sz++;
		left -= used;
		if (c & 0x80)
		{
			if (p + sz > n || op + sz > size)
				goto bad;
			memcpy (o + op, b + p, sz);
			p += sz;
			op += sz;
			left -= sz;
		}
		else
		{
			off++;
			if (off > op || op + sz > size)
				goto bad;
			for (uint i = 0; i < sz; i++, op++)
				o[op] = o[op - off];
		}
	}
	if (op != size)
		goto bad;
	if (xor)
		for (size_t i = 0; i < size; i++)
			o[i] ^= (u8) xor ;
	*out = o;
	*out_size = size;
	return ERR_OK;
bad:
	FREE (o);
	return ERR_INVALID_DATA;
}

void ResetToshiTrb (toshi_trb_t *t)
{
	FREE (t->sect);
	memset (t, 0, sizeof (*t));
}

enumError OpenToshiTrb (toshi_trb_t *t, const u8 *d, size_t size)
{
	memset (t, 0, sizeof (*t));
	if (!IsToshiTsfb (d, size))
		return ERR_INVALID_DATA;
	size_t p = 12;
	enumError err = ERR_OK;
	while (!err && p + 8 <= size)
	{
		const u32 s = ts_be32 (d + p + 4);
		const u8 *pl = d + p + 8;
		if (s > size - p - 8)
			break;
		if (!memcmp (d + p, "TCES", 4) && !t->sect)
		{
			t->sect = MALLOC (s ? s : 1);
			if (!t->sect)
				return ERR_OUT_OF_MEMORY;
			memcpy (t->sect, pl, s);
			t->sect_size = s;
		}
		else if (!memcmp (d + p, "CCES", 4) && !t->sect)
			err = ts_btec (&t->sect, &t->sect_size, pl, s);
		else if (!memcmp (d + p, "BMYS", 4))
			t->symb = pl, t->symb_size = s;
		p += 8 + ((size_t)s + 3 & ~(size_t)3);
	}
	if (!err && (!t->sect || !t->symb || t->symb_size < 4))
		err = ERR_INVALID_DATA;
	if (err)
		ResetToshiTrb (t);
	return err;
}

s64 ToshiSymbol (const toshi_trb_t *t, ccp name)
{
	const u32 n = ts_be32 (t->symb);
	if (n > (t->symb_size - 4) / 12)
		return -1;
	const size_t names = 4 + 12 * (size_t)n;
	for (u32 i = 0; i < n; i++)
	{
		const u8 *e = t->symb + 4 + 12 * i;
		const size_t no = names + ts_be16 (e + 2);
		if (no < t->symb_size && !strncmp ((ccp)t->symb + no, name, t->symb_size - no))
		{
			const u32 off = ts_be32 (e + 8);
			return off < t->sect_size ? (s64)off : -1;
		}
	}
	return -1;
}

// 64-bit on purpose: callers pass count * stride products that must not be
// truncated before the check.
static bool ts_ptr (const toshi_trb_t *t, u64 off, u64 len)
{
	return off <= t->sect_size && len <= t->sect_size - off;
}

static uint ts_ttl_entries (const toshi_trb_t *t, u32 *ents)
{
	const s64 base = ToshiSymbol (t, "TTL");
	if (base < 0 || !ts_ptr (t, base, 12))
		return 0;
	const u32 n = ts_be32 (t->sect + base), po = ts_be32 (t->sect + base + 4);
	if (!n || n > 65536 || !ts_ptr (t, po, n * 52))
		return 0;
	*ents = po;
	return n;
}

uint ToshiTtlCount (const toshi_trb_t *t)
{
	u32 po;
	return ts_ttl_entries (t, &po);
}

enumError DecodeToshiTexture (
	const toshi_trb_t *t, uint idx, ccp *name, u8 **rgba, uint *width, uint *height)
{
	u32 po;
	const uint n = ts_ttl_entries (t, &po);
	if (idx >= n)
		return ERR_INVALID_DATA;
	const u8 *e = t->sect + po + 52 * idx;
	const u32 fmt = ts_be32 (e), np = ts_be32 (e + 4), w = ts_be32 (e + 8), h = ts_be32 (e + 12);
	const u32 dp = ts_be32 (e + 20), dsz = ts_be32 (e + 24), pp = ts_be32 (e + 28);
	const u32 npal = ts_be32 (e + 36);
	(void)e;
	static const u8 gx[] = { 0, 2, 1, 3, 4, 5, 6, 14 }; // 0x301..0x308
	static const u8 ci_gx[] = { 8, 8, 9, 9, 9 }, ci_pal[] = { 1, 2, 1, 2, 0 }; // 0x30c..0x310
	uint gxfmt;
	const u8 *pal = 0;
	uint pal_fmt = 0;
	if (fmt >= 0x301 && fmt <= 0x308)
		gxfmt = gx[fmt - 0x301];
	else if (fmt >= 0x30c && fmt <= 0x310)
	{
		gxfmt = ci_gx[fmt - 0x30c];
		pal_fmt = ci_pal[fmt - 0x30c];
		if (!npal || npal > 256 || !ts_ptr (t, pp, npal * 2))
			return ERR_INVALID_DATA;
		pal = t->sect + pp;
	}
	else
		return ERR_INVALID_DATA;
	if (!w || !h || w > 4096 || h > 4096 || !ts_ptr (t, dp, dsz) || !ts_ptr (t, np, 1))
		return ERR_INVALID_DATA;
	*name = (ccp)t->sect + np;
	const enumError err
		= DecodeGXTexture_RGBA (rgba, w, h, gxfmt, t->sect + dp, dsz, pal, npal, pal_fmt);
	if (!err)
		*width = w, *height = h;
	return err;
}

static s32 ts_be32s (const u8 *p)
{
	return (s32)ts_be32 (p);
}

static float ts_bef32 (const u8 *p)
{
	const u32 u = ts_be32 (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

enumError OpenToshiTkl (const toshi_trb_t *t, toshi_tkl_t *tkl)
{
	memset (tkl, 0, sizeof (*tkl));
	const s64 base = ToshiSymbol (t, "keylib");
	if (base < 0 || !ts_ptr (t, base, 52))
		return ERR_INVALID_DATA;
	const u8 *h = t->sect + base;
	const u32 no = ts_be32 (h);
	if (!ts_ptr (t, no, 1))
		return ERR_INVALID_DATA;
	tkl->name = (ccp)t->sect + no;
	tkl->scale[0] = ts_bef32 (h + 4);
	tkl->scale[1] = ts_bef32 (h + 8);
	tkl->scale[2] = ts_bef32 (h + 12);
	tkl->num_t = (u32)ts_be32s (h + 16);
	tkl->num_q = (u32)ts_be32s (h + 20);
	tkl->num_s = (u32)ts_be32s (h + 24);
	tkl->tsize = (u32)ts_be32s (h + 28);
	tkl->qsize = (u32)ts_be32s (h + 32);
	tkl->ssize = (u32)ts_be32s (h + 36);
	const u32 to = ts_be32 (h + 40), qo = ts_be32 (h + 44), so = ts_be32 (h + 48);
	if (!ts_ptr (t, to, (u64)tkl->num_t * tkl->tsize)
		|| !ts_ptr (t, qo, (u64)tkl->num_q * tkl->qsize)
		|| !ts_ptr (t, so, (u64)tkl->num_s * tkl->ssize))
		return ERR_INVALID_DATA;
	tkl->t_data = t->sect + to;
	tkl->q_data = t->sect + qo;
	tkl->s_data = t->sect + so;
	return ERR_OK;
}

bool ToshiTklTranslation (const toshi_tkl_t *t, uint idx, float out[3])
{
	if (idx >= t->num_t || t->tsize < 6)
		return false;
	const u8 *p = t->t_data + (size_t)idx * t->tsize;
	for (uint i = 0; i < 3; i++)
		out[i] = (s16)ts_be16 (p + 2 * i) * t->scale[i];
	return true;
}

bool ToshiTklQuaternion (const toshi_tkl_t *t, uint idx, float out[4])
{
	if (idx >= t->num_q || t->qsize < 8)
		return false;
	const u8 *p = t->q_data + (size_t)idx * t->qsize;
	for (uint i = 0; i < 4; i++)
		out[i] = (s16)ts_be16 (p + 2 * i) / 32767.0f;
	return true;
}

bool ToshiTklScale (const toshi_tkl_t *t, uint idx, float *out)
{
	if (idx >= t->num_s)
		return false;
	const u8 *p = t->s_data + (size_t)idx * t->ssize;
	// BEST-EFFORT: no disc sample with numScales>0 has been found to confirm
	// this branch; float is the natural size match, s16/32767 is by analogy
	// with the verified quaternion encoding.
	*out = t->ssize >= 4 ? ts_bef32 (p) : t->ssize == 2 ? (s16)ts_be16 (p) / 32767.0f : 0.0f;
	return true;
}

//-----------------------------------------------------------------------------
// Model .trb -> model_t; see lib-toshi.h for the layout.
//-----------------------------------------------------------------------------

#include <dirent.h>
#include <math.h>
#include "lib-image.h"

// model_t fields are released with plain free() by FreeModel(), so the
// allocations that escape into it must not use the tracked allocator.
#undef calloc
#undef malloc
#undef realloc
#undef free

#define TS_BONE_SIZE 192
#define TS_MAT_SIZE 296
#define TS_TEX_UV_FRAC 8

bool IsToshiModel (const toshi_trb_t *t)
{
	return ToshiSymbol (t, "Skeleton") >= 0 && ToshiSymbol (t, "Materials") >= 0
		&& ToshiSymbol (t, "Header") >= 0 && ToshiSymbol (t, "LOD0_Mesh_0") >= 0;
}

// "Prop\\copcar2.tga" -> "Prop_copcar2", the name extract_toshi_ttl_file gives a texture.
static void ts_tex_base (ccp name, char *out, size_t out_size)
{
	snprintf (out, out_size, "%.120s", name);
	char *dot = strrchr (out, '.');
	if (dot && strlen (dot) <= 5)
		*dot = 0;
	for (char *c = out; *c; c++)
		if (*c == '\\' || *c == '/' || *c == ':')
			*c = '_';
}

// Position / normal / texcoord index of one display list vertex.
typedef struct
{
	u32 pi, ni, ui;
	int node;
} ts_vtx_t;

static void ts_quat_to_euler (const float q[4], vec3_t *e)
{
	const double x = q[0], y = q[1], z = q[2], w = q[3];
	double s = 2.0 * (w * y - z * x);
	s = s > 1 ? 1 : s < -1 ? -1 : s;
	e->x = (float)(atan2 (2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y)) * 180.0 / M_PI);
	e->y = (float)(asin (s) * 180.0 / M_PI);
	e->z = (float)(atan2 (2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z)) * 180.0 / M_PI);
}

static bool ts_build_mesh (const toshi_trb_t *t, model_t *m, uint mesh_no, uint node_base,
	uint n_skin, const u8 *hdr, ccp name, bool skinned)
{
	const u8 *s = t->sect;
	const size_t sz = t->sect_size;
	const u32 pos_o = ts_be32 (hdr), nrm_o = ts_be32 (hdr + 4), uv_o = ts_be32 (hdr + 8);
	// Skin-shader meshes: {sub table, sub count, material, skin table}; the other shaders
	// carry one display list inline: {dl, dl size, material, vertex count}.
	const u32 sub_o = ts_be32 (hdr + 12), n_sub = skinned ? ts_be32 (hdr + 16) : 1;
	const u32 mat_o = ts_be32 (hdr + 20);
	const uint frac = hdr[28] & 31;
	const uint isz[3] = { hdr[29] == 2 ? 1 : 2, hdr[30] == 2 ? 1 : 2, hdr[31] == 2 ? 1 : 2 };
	if (!n_sub || n_sub > 256 || (skinned && !ts_ptr (t, sub_o, (u64)n_sub * 24))
		|| !ts_ptr (t, pos_o, 6) || !ts_ptr (t, nrm_o, 3) || !ts_ptr (t, uv_o, 4)
		|| !ts_ptr (t, mat_o, 1))
		return false;
	const uint vsz = (skinned ? 1 : 0) + isz[0] + isz[1] + isz[2];

	// expanded triangle corners
	size_t cap = 3 * 1024, n = 0;
	ts_vtx_t *corner = MALLOC (cap * sizeof (*corner));
	if (!corner)
		return false;

	for (uint k = 0; k < n_sub; k++)
	{
		const u8 *e = skinned ? s + sub_o + 24 * k : hdr + 12;
		const u32 dl = ts_be32 (e), dsz = ts_be32 (e + 4);
		if (!ts_ptr (t, dl, dsz))
			continue;
		const u8 *bones = skinned ? e + 12 : 0;
		u32 p = 0;
		while (p + 3 <= dsz && s[dl + p])
		{
			const uint cmd = s[dl + p], cnt = ts_be16 (s + dl + p + 1);
			p += 3;
			if ((u64)cnt * vsz > dsz - p)
				break;
			ts_vtx_t *v = MALLOC ((cnt ? cnt : 1) * sizeof (*v));
			if (!v)
				break;
			for (uint i = 0; i < cnt; i++)
			{
				const u8 *q = s + dl + p + (size_t)i * vsz;
				const uint slot = skinned ? q[0] / 3 : 12;
				const uint skin = slot < 12 ? bones[slot] : 0xff;
				uint o = skinned ? 1 : 0;
				u32 idx[3];
				for (uint a = 0; a < 3; a++)
				{
					idx[a] = isz[a] == 1 ? q[o] : ts_be16 (q + o);
					o += isz[a];
				}
				v[i] = (ts_vtx_t) { idx[0], idx[1], idx[2],
					skin < n_skin ? (int)(node_base + skin) : -1 };
			}
			p += cnt * vsz;
			// GX primitive -> triangles
			uint tri[4096 * 3];
			uint nt = 0;
			for (uint i = 0; cnt >= 3 && i + 2 < cnt && nt + 6 <= sizeof (tri) / sizeof (*tri);
				 i++)
			{
				uint a, b, c;
				if (cmd == 0x90)
				{
					if (i % 3)
						continue;
					a = i, b = i + 1, c = i + 2;
				}
				else if (cmd == 0x98)
				{
					a = i & 1 ? i + 1 : i, b = i & 1 ? i : i + 1, c = i + 2;
				}
				else if (cmd == 0xa0)
				{
					a = 0, b = i + 1, c = i + 2;
				}
				else
					break;
				if (v[a].pi == v[b].pi || v[b].pi == v[c].pi || v[a].pi == v[c].pi)
					continue;
				tri[nt++] = a, tri[nt++] = b, tri[nt++] = c;
			}
			if (cmd == 0x80)
				for (uint i = 0; i + 3 < cnt && nt + 6 <= sizeof (tri) / sizeof (*tri); i += 4)
				{
					const uint quad[6] = { i, i + 1, i + 2, i, i + 2, i + 3 };
					for (uint j = 0; j < 6; j++)
						tri[nt++] = quad[j];
				}
			for (uint j = 0; j < nt; j++)
			{
				if (n == cap)
				{
					cap *= 2;
					ts_vtx_t *nc = REALLOC (corner, cap * sizeof (*corner));
					if (!nc)
					{
						FREE (v);
						FREE (corner);
						return false;
					}
					corner = nc;
				}
				corner[n++] = v[tri[j]];
			}
			FREE (v);
			if (cmd != 0x90 && cmd != 0x98 && cmd != 0xa0 && cmd != 0x80)
				break;
		}
	}
	if (!n)
	{
		FREE (corner);
		return false;
	}

	mesh_t *nm = realloc (m->meshes, (m->num_meshes + 1) * sizeof (*nm));
	if (!nm)
	{
		FREE (corner);
		return false;
	}
	m->meshes = nm;
	mesh_t *mesh = m->meshes + m->num_meshes++;
	memset (mesh, 0, sizeof (*mesh));
	snprintf (mesh->name, sizeof (mesh->name), "%s", name);

	// unique positions keyed by (node, index); normals and texcoords by index
	uint tab_size = 1024;
	while (tab_size < n * 2)
		tab_size <<= 1;
	u64 *tk = MALLOC (tab_size * sizeof (u64));
	int *tv = MALLOC (tab_size * sizeof (int));
	int *nmap = MALLOC (65536 * sizeof (int)), *umap = MALLOC (65536 * sizeof (int));
	mesh->vertices = calloc (n, sizeof (vertex_t));
	mesh->positions = calloc (n, sizeof (vec3_t));
	mesh->position_node = calloc (n, sizeof (int));
	mesh->normals = calloc (n, sizeof (vec3_t));
	mesh->texcoords = calloc (n, sizeof (vec2_t));
	if (!tk || !tv || !nmap || !umap || !mesh->vertices || !mesh->positions
		|| !mesh->position_node || !mesh->normals || !mesh->texcoords)
	{
		FREE (tk);
		FREE (tv);
		FREE (nmap);
		FREE (umap);
		FREE (corner);
		return false;
	}
	memset (tk, 0xff, tab_size * sizeof (u64));
	memset (nmap, 0xff, 65536 * sizeof (int));
	memset (umap, 0xff, 65536 * sizeof (int));

	const float ps = 1.0f / (float)(1u << frac), us = 1.0f / (float)(1u << TS_TEX_UV_FRAC);
	for (size_t i = 0; i < n; i++)
	{
		const ts_vtx_t *c = corner + i;
		vertex_t *vx = mesh->vertices + i;
		vx->tangent_idx = vx->matrix_idx = -1;
		vx->color_idx[0] = vx->color_idx[1] = -1;
		for (int e = 0; e < 7; e++)
			vx->extra_texcoord_idx[e] = -1;

		const u64 key = ((u64)(u32)(c->node + 1) << 32) | c->pi;
		uint h = (uint)((key * 0x9E3779B97F4A7C15ull) >> 40) & (tab_size - 1);
		while (tk[h] != ~0ull && tk[h] != key)
			h = (h + 1) & (tab_size - 1);
		if (tk[h] == ~0ull)
		{
			tk[h] = key;
			tv[h] = (int)mesh->num_positions;
			vec3_t P = { 0, 0, 0 };
			if (ts_ptr (t, pos_o + 6ull * c->pi, 6))
			{
				const u8 *q = s + pos_o + 6 * c->pi;
				// Z-up source -> Y-up: (x, y, z) -> (x, z, -y)
				const float x = (s16)ts_be16 (q) * ps, y = (s16)ts_be16 (q + 2) * ps,
							z = (s16)ts_be16 (q + 4) * ps;
				P = (vec3_t) { x, z, -y };
			}
			mesh->position_node[mesh->num_positions] = c->node;
			mesh->positions[mesh->num_positions++] = P;
		}
		vx->position_idx = tv[h];

		if (nmap[c->ni & 0xffff] < 0)
		{
			nmap[c->ni & 0xffff] = (int)mesh->num_normals;
			vec3_t N = { 0, 1, 0 };
			if (ts_ptr (t, nrm_o + 3ull * c->ni, 3))
			{
				const u8 *q = s + nrm_o + 3 * c->ni;
				const float x = (int8_t)q[0], y = (int8_t)q[1], z = (int8_t)q[2];
				const float len = sqrtf (x * x + y * y + z * z);
				if (len > 0)
					N = (vec3_t) { x / len, z / len, -y / len };
			}
			mesh->normals[mesh->num_normals++] = N;
		}
		vx->normal_idx = nmap[c->ni & 0xffff];

		if (umap[c->ui & 0xffff] < 0)
		{
			umap[c->ui & 0xffff] = (int)mesh->num_texcoords;
			vec2_t U = { 0, 0 };
			if (ts_ptr (t, uv_o + 4ull * c->ui, 4))
			{
				const u8 *q = s + uv_o + 4 * c->ui;
				U = (vec2_t) { (s16)ts_be16 (q) * us, (s16)ts_be16 (q + 2) * us };
			}
			mesh->texcoords[mesh->num_texcoords++] = U;
		}
		vx->texcoord_idx = umap[c->ui & 0xffff];
	}
	mesh->num_vertices = n;
	FREE (tk);
	FREE (tv);
	FREE (nmap);
	FREE (umap);
	FREE (corner);

	mesh->material_idx = 0;
	const size_t mlen = strnlen ((ccp)s + mat_o, sz - mat_o);
	for (uint i = 0; i < m->num_materials; i++)
		if (strlen (m->materials[i].name) == mlen && !memcmp (m->materials[i].name, s + mat_o, mlen))
			mesh->material_idx = (int)i;
	(void)mesh_no;
	return true;
}

model_t *BuildToshiModel (const toshi_trb_t *t)
{
	const u8 *s = t->sect;
	const s64 sk = ToshiSymbol (t, "Skeleton"), mt = ToshiSymbol (t, "Materials");
	if (sk < 0 || mt < 0 || !ts_ptr (t, sk, 0x48))
		return 0;
	const uint nb = ts_be16 (s + sk);
	if (!nb || nb > 512 || !ts_ptr (t, sk + 0x48, (u64)nb * TS_BONE_SIZE)
		|| !ts_ptr (t, mt, 16 + 0))
		return 0;
	const uint nmat = ts_be32 (s + mt + 8);
	if (nmat > 1024 || !ts_ptr (t, mt + 16, (u64)nmat * TS_MAT_SIZE))
		return 0;

	model_t *m = calloc (1, sizeof (*m));
	if (!m)
		return 0;
	m->joints = calloc (nb, sizeof (joint_t));
	m->materials = calloc (nmat ? nmat : 1, sizeof (material_t));
	if (!m->joints || !m->materials)
	{
		FreeModel (m);
		return 0;
	}
	m->num_joints = nb;
	m->num_materials = nmat;

	// Rx(-90 deg): the Z-up source basis to glTF's Y-up
	static const float C[4] = { -0.70710678f, 0, 0, 0.70710678f };
	for (uint i = 0; i < nb; i++)
	{
		const u8 *b = s + sk + 0x48 + (size_t)i * TS_BONE_SIZE;
		joint_t *j = m->joints + i;
		const uint ln = b[0x90] > 31 ? 31 : b[0x90];
		snprintf (j->name, sizeof (j->name), "%.*s", (int)ln, (ccp)b + 0x91);
		if (!j->name[0])
			snprintf (j->name, sizeof (j->name), "bone%u", i);
		j->parent_idx = (s16)ts_be16 (b + 0xb0);
		if (j->parent_idx >= (int)nb || j->parent_idx == (int)i)
			j->parent_idx = -1;
		float q[4] = { ts_bef32 (b), ts_bef32 (b + 4), ts_bef32 (b + 8), ts_bef32 (b + 12) };
		float x = ts_bef32 (b + 0xb4), y = ts_bef32 (b + 0xb8), z = ts_bef32 (b + 0xbc);
		if (j->parent_idx < 0)
		{
			// q = C * q
			const float r[4] = { C[3] * q[0] + C[0] * q[3] + C[1] * q[2] - C[2] * q[1],
				C[3] * q[1] + C[1] * q[3] + C[2] * q[0] - C[0] * q[2],
				C[3] * q[2] + C[2] * q[3] + C[0] * q[1] - C[1] * q[0],
				C[3] * q[3] - C[0] * q[0] - C[1] * q[1] - C[2] * q[2] };
			memcpy (q, r, sizeof (q));
			const float ty = y;
			y = z, z = -ty;
		}
		ts_quat_to_euler (q, &j->rotate);
		j->translate = (vec3_t) { x, y, z };
		j->scale = (vec3_t) { 1, 1, 1 };
	}
	if (!ComputeModelTRSBinds (m))
	{
		FreeModel (m);
		return 0;
	}

	for (uint i = 0; i < nmat; i++)
	{
		const u8 *e = s + mt + 16 + (size_t)i * TS_MAT_SIZE;
		material_t *mat = m->materials + i;
		snprintf (mat->name, sizeof (mat->name), "%.63s", (ccp)e);
		if (e[104])
		{
			ts_tex_base ((ccp)e + 104, mat->textures[0], sizeof (mat->textures[0]));
			mat->num_textures = 1;
		}
		mat->diffuse[0] = mat->diffuse[1] = mat->diffuse[2] = mat->diffuse[3] = 1.0f;
	}

	// LOD0 shader: 0 = skin (matrix bytes, skin table), others = static meshes
	uint shader = 0;
	const s64 ho = ToshiSymbol (t, "Header");
	if (ho >= 0 && ts_ptr (t, ho, 12))
	{
		const u32 lod = ts_be32 (s + ho + 8);
		if (ts_ptr (t, lod, 12))
			shader = ts_be32 (s + lod + 8);
	}

	// meshes of LOD0; each mesh brings its own skin table
	for (uint k = 0; k < 64; k++)
	{
		char name[32];
		snprintf (name, sizeof (name), "LOD0_Mesh_%u", k);
		const s64 mo = ToshiSymbol (t, name);
		if (mo < 0 || !ts_ptr (t, mo, 32))
			break;
		const u8 *hdr = s + mo;
		const bool skinned = shader == 0;
		const u32 skin_o = ts_be32 (hdr + 24);
		uint n_skin = 0, node_base = (uint)m->num_node_influences;
		if (skinned && ts_ptr (t, skin_o, 8))
		{
			const u32 cnt = ts_be32 (s + skin_o), tab = ts_be32 (s + skin_o + 4);
			if (cnt && cnt <= 4096 && ts_ptr (t, tab, (u64)cnt * 16))
			{
				node_influence_t *ni
					= realloc (m->node_influences, (node_base + cnt) * sizeof (*ni));
				if (ni)
				{
					m->node_influences = ni;
					memset (ni + node_base, 0, cnt * sizeof (*ni));
					m->num_node_influences = node_base + cnt;
					n_skin = cnt;
					for (uint j = 0; j < cnt; j++)
					{
						const u8 *e = s + tab + 16 * j;
						const uint c = e[0] > 3 ? 3 : e[0];
						node_influence_t *n = ni + node_base + j;
						n->weights = calloc (c ? c : 1, sizeof (influence_t));
						if (!n->weights)
							continue;
						for (uint w = 0; w < c; w++)
							n->weights[n->num_weights++]
								= (influence_t) { e[1 + w] < nb ? e[1 + w] : 0,
									  ts_bef32 (e + 4 + 4 * w) };
					}
				}
			}
		}
		ts_build_mesh (t, m, k, node_base, n_skin, hdr, name, skinned);
	}
	if (!m->num_meshes)
	{
		FreeModel (m);
		return 0;
	}
	return m;
}

static void ts_export_ttl (const toshi_trb_t *t, ccp dest_dir, char (*want)[64], bool *done, uint n)
{
	const uint count = ToshiTtlCount (t);
	for (uint i = 0; i < count; i++)
	{
		u8 *rgba = 0;
		uint w = 0, h = 0;
		ccp tex_name = 0;
		if (DecodeToshiTexture (t, i, &tex_name, &rgba, &w, &h))
			continue;
		char base[128];
		ts_tex_base (tex_name, base, sizeof (base));
		bool need = false;
		for (uint j = 0; j < n; j++)
			if (!done[j] && !strcasecmp (want[j], base))
				done[j] = need = true;
		if (!need)
		{
			FREE (rgba);
			continue;
		}
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s.png", dest_dir, base);
		SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true);
	}
}

static bool ts_load_ttl (ccp path, toshi_trb_t *trb, u8 **raw)
{
	FILE *f = fopen (path, "rb");
	if (!f)
		return false;
	fseek (f, 0, SEEK_END);
	const long len = ftell (f);
	fseek (f, 0, SEEK_SET);
	*raw = len > 0 && len < (1L << 30) ? MALLOC (len) : 0;
	const bool ok = *raw && fread (*raw, 1, len, f) == (size_t)len
		&& !OpenToshiTrb (trb, *raw, len);
	fclose (f);
	if (!ok)
	{
		FREE (*raw);
		*raw = 0;
	}
	return ok;
}

void ExportToshiModelTextures (const model_t *model, ccp model_path, ccp dest_dir)
{
	uint n = 0;
	char (*want)[64] = CALLOC (model->num_materials ? model->num_materials : 1, sizeof (*want));
	bool *done = CALLOC (model->num_materials ? model->num_materials : 1, sizeof (bool));
	if (!want || !done)
		goto out;
	for (uint i = 0; i < model->num_materials; i++)
		if (model->materials[i].num_textures)
			snprintf (want[n++], sizeof (*want), "%s", model->materials[i].textures[0]);
	if (!n)
		goto out;

	// <dir>/../Matlibs/<model>.ttl first, then every library there
	char dir[PATH_MAX], stem[PATH_MAX], lib_dir[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", model_path);
	char *slash = strrchr (dir, '/');
	if (slash)
		*slash++ = 0;
	else
		snprintf (dir, sizeof (dir), "."), slash = dir;
	snprintf (stem, sizeof (stem), "%s", slash == dir ? model_path : slash);
	char *dot = strrchr (stem, '.');
	if (dot)
		*dot = 0;
	snprintf (lib_dir, sizeof (lib_dir), "%s/../Matlibs", dir);
	CreatePath (dest_dir, true);

	for (uint pass = 0; pass < 2; pass++)
	{
		DIR *dp = pass ? opendir (lib_dir) : 0;
		const struct dirent *de = 0;
		char path[PATH_MAX];
		for (;;)
		{
			if (pass)
			{
				if (!dp || !(de = readdir (dp)))
					break;
				const size_t l = strlen (de->d_name);
				if (l < 5 || strcasecmp (de->d_name + l - 4, ".ttl"))
					continue;
				snprintf (path, sizeof (path), "%s/%s", lib_dir, de->d_name);
			}
			else
				snprintf (path, sizeof (path), "%s/%s.ttl", lib_dir, stem);
			bool all = true;
			for (uint j = 0; j < n; j++)
				all = all && done[j];
			if (all)
				break;
			toshi_trb_t trb;
			u8 *raw = 0;
			if (ts_load_ttl (path, &trb, &raw))
			{
				ts_export_ttl (&trb, dest_dir, want, done, n);
				ResetToshiTrb (&trb);
				FREE (raw);
			}
			if (!pass)
				break;
		}
		if (dp)
			closedir (dp);
	}
out:
	FREE (want);
	FREE (done);
}
