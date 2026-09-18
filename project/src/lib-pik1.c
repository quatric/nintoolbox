// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-pik1.h"
#include "lib-model-glb.h"
#include "lib-excite.h"
#include "lib-image.h"
#include <string.h>
#include <math.h>

static inline u16 pk_be16 (const u8 *p)
{
	return (u16)((u16)p[0] << 8 | p[1]);
}

static inline s16 pk_be16s (const u8 *p)
{
	return (s16)pk_be16 (p);
}

static inline u32 pk_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline s32 pk_be32s (const u8 *p)
{
	return (s32)pk_be32 (p);
}

static inline float pk_bef32 (const u8 *p)
{
	u32 u = pk_be32 (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

static inline void pk_wr16 (u8 *p, u16 v)
{
	p[0] = (u8)(v >> 8);
	p[1] = (u8)v;
}

static inline void pk_wr32 (u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

static inline void pk_wrf32 (u8 *p, float f)
{
	u32 u;
	memcpy (&u, &f, 4);
	pk_wr32 (p, u);
}

//-----------------------------------------------------------------------------
///////////////		TXE texture					///////////////
//-----------------------------------------------------------------------------

static int pk_txe_gx (uint fmt, uint *need, uint w, uint h)
{
	uint gx;
	switch (fmt)
	{
		case 0:
			gx = 4;
			break; // RGB565
		case 1:
			gx = 14;
			break; // CMPR
		case 2:
			gx = 5;
			break; // RGB5A3
		case 3:
			gx = 0;
			break; // I4
		case 4:
			gx = 1;
			break; // I8
		case 5:
			gx = 2;
			break; // IA4
		case 6:
			gx = 3;
			break; // IA8
		case 7:
			gx = 6;
			break; // RGBA32
		default:
			return -1;
	}
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
			return -1;
	}
	*need = ((w + bw - 1) / bw) * ((h + bh - 1) / bh) * (bw * bh * bpp / 8);
	return (int)gx;
}

bool IsTXE (const u8 *data, size_t size)
{
	if (!data || size < 12)
		return false;
	const uint w = pk_be16 (data), h = pk_be16 (data + 2), fmt = pk_be16 (data + 6);
	const uint decl = pk_be32 (data + 8);
	if (!w || !h || w > 2048 || h > 2048 || fmt > 7)
		return false;
	uint need = 0;
	if (pk_txe_gx (fmt, &need, w, h) < 0)
		return false;
	if (decl != need)
		return false;
	// pixels start at the next 32-byte boundary after the 12-byte header
	const uint base = (12 + 31) & ~31u;
	return (u64)base + need <= size;
}

enumError DecodeTXE_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	if (!dest || !width || !height || !IsTXE (src, src_size))
		return ERR_INVALID_DATA;
	const uint w = pk_be16 (src), h = pk_be16 (src + 2), fmt = pk_be16 (src + 6);
	uint need = 0;
	const int gx = pk_txe_gx (fmt, &need, w, h);
	const uint base = (12 + 31) & ~31u;
	u8 *rgba = 0;
	if (DecodeGXTexture_RGBA (&rgba, w, h, (uint)gx, src + base, need, 0, 0, 0))
		return ERR_INVALID_DATA;
	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

enumError DecodeTXE (const u8 *data, uint size, ccp out_path)
{
	u8 *rgba = 0;
	uint w = 0, h = 0;
	if (DecodeTXE_RGBA (&rgba, &w, &h, data, size))
		return ERR_NOTHING_TO_DO;
	if (SaveDecodedRGBAToPNG (rgba, w, h, &be_func, out_path, 0, true))
		return ERR_CANT_CREATE;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		Pikmin MOD model				///////////////
//-----------------------------------------------------------------------------

enum
{
	PKC_HEADER = 0x0000,
	PKC_POS = 0x0010,
	PKC_NRM = 0x0011,
	PKC_NBT = 0x0012,
	PKC_COL = 0x0013,
	PKC_UV0 = 0x0018,
	PKC_TEX = 0x0020,
	PKC_TXATTR = 0x0022,
	PKC_MAT = 0x0030,
	PKC_SKIN = 0x0040,
	PKC_ENV = 0x0041,
	PKC_MESH = 0x0050,
	PKC_JOINT = 0x0060,
	PKC_JNAME = 0x0061,
	PKC_EOF = 0xffff,
};

#define PK_MAX_CHUNKS 4096
#define PK_MAX_VERTS (8u << 20)

typedef struct
{
	int op;
	u32 off, size; // payload offset/size (after the 8-byte chunk head)
} pk_chunk_t;

// Full structural walk: every chunk 32-aligned, inside the file, opcodes
// known, exactly one header first and one EOF terminator.
static bool pk_walk (const u8 *data, uint size, pk_chunk_t *out, uint *n_out)
{
	uint n = 0, pos = 0;
	bool seen_head = false, seen_eof = false;
	while (pos + 8 <= size)
	{
		if ((pos & 31) || n >= PK_MAX_CHUNKS)
			return false;
		const int op = pk_be32s (data + pos);
		const u32 sz = pk_be32 (data + pos + 4);
		if ((u64)pos + 8 + sz > size)
			return false;
		switch (op)
		{
			case PKC_HEADER:
			case PKC_POS:
			case PKC_NRM:
			case PKC_NBT:
			case PKC_COL:
			case PKC_UV0:
			case PKC_UV0 + 1:
			case PKC_UV0 + 2:
			case PKC_UV0 + 3:
			case PKC_UV0 + 4:
			case PKC_UV0 + 5:
			case PKC_UV0 + 6:
			case PKC_UV0 + 7:
			case PKC_TEX:
			case PKC_TXATTR:
			case PKC_MAT:
			case PKC_SKIN:
			case PKC_ENV:
			case PKC_MESH:
			case PKC_JOINT:
			case PKC_JNAME:
			case 0x0100:
			case 0x0110:
				break;
			case PKC_EOF:
				seen_eof = true;
				break;
			default:
				return false;
		}
		if (op == PKC_HEADER)
		{
			if (seen_head || pos != 0 || sz < 32)
				return false;
			seen_head = true;
		}
		if (out)
		{
			out[n].op = op;
			out[n].off = pos + 8;
			out[n].size = sz;
		}
		n++;
		if (op == PKC_EOF)
			break;
		pos += 8 + sz;
		// next chunk starts at the next 32-byte boundary
		pos = (pos + 31) & ~31u;
	}
	if (!seen_head || !seen_eof)
		return false;
	if (n_out)
		*n_out = n;
	return true;
}

bool IsPIKMOD (const u8 *data, size_t size)
{
	return data && size <= 0xffffffffu && pk_walk (data, (uint)size, 0, 0);
}

static const pk_chunk_t *pk_find (const pk_chunk_t *c, uint n, int op)
{
	for (uint i = 0; i < n; i++)
		if (c[i].op == op)
			return c + i;
	return 0;
}

// pool base: u32 count + align(0x20) from chunk start
static bool pk_pool (const u8 *data, uint size, const pk_chunk_t *c, uint *count, uint *base)
{
	if (!c || c->size < 4)
		return false;
	*count = pk_be32 (data + c->off);
	uint b = c->off + 4;
	b = (b + 31) & ~31u;
	if (b > size)
		return false;
	*base = b;
	return true;
}

typedef struct
{
	int pos, nrm, col, uv, slot;
} pk_corner_t;

typedef struct
{
	pk_corner_t *v;
	size_t num, cap;
} pk_soup_t;

static bool pk_push (pk_soup_t *s, pk_corner_t c)
{
	if (s->num >= s->cap)
	{
		const size_t nc = s->cap ? s->cap * 2 : 256;
		pk_corner_t *nn = REALLOC (s->v, nc * sizeof (*nn));
		if (!nn)
			return false;
		s->v = nn;
		s->cap = nc;
	}
	s->v[s->num++] = c;
	return true;
}

static bool pk_tris (pk_soup_t *out, u8 op, const pk_corner_t *v, uint n)
{
	if (op == 0x90)
	{
		for (uint i = 0; i + 2 < n; i += 3)
			if (!pk_push (out, v[i]) || !pk_push (out, v[i + 1]) || !pk_push (out, v[i + 2]))
				return false;
		return true;
	}
	if (n < 3)
		return true;
	if (op == 0xa0)
	{
		for (uint i = 0; i < 3 && i < n; i++)
			if (!pk_push (out, v[i]))
				return false;
		for (uint i = 3; i < n; i++)
		{
			const pk_corner_t a = v[0], b = v[i - 1], d = v[i];
			if ((a.pos != b.pos || a.uv != b.uv) && (b.pos != d.pos || b.uv != d.uv)
				&& (d.pos != a.pos || d.uv != a.uv))
				if (!pk_push (out, b) || !pk_push (out, d) || !pk_push (out, a))
					return false;
		}
		return true;
	}
	for (uint i = 2; i < n; i++)
	{
		pk_corner_t v0 = (i % 2) == 0 ? v[i - 2] : v[i - 1];
		pk_corner_t v1 = (i % 2) == 0 ? v[i] : v[i - 2];
		pk_corner_t v2 = (i % 2) == 0 ? v[i - 1] : v[i];
		if ((v0.pos != v1.pos || v0.uv != v1.uv) && (v1.pos != v2.pos || v1.uv != v2.uv)
			&& (v2.pos != v0.pos || v2.uv != v0.uv))
			if (!pk_push (out, v1) || !pk_push (out, v2) || !pk_push (out, v0))
				return false;
	}
	return true;
}

typedef struct
{
	float x, y, z;
} pk_f3_t;

static void pk_trs_world (float out[12], const float t[3], const float r[3], const float s[3],
	const float parent[12], bool has_parent)
{
	const double dx = r[0] * (M_PI / 180.0), dy = r[1] * (M_PI / 180.0), dz = r[2] * (M_PI / 180.0);
	const float cx = cosf ((float)dx), sx = sinf ((float)dx), cy = cosf ((float)dy),
				sy = sinf ((float)dy), cz = cosf ((float)dz), sz = sinf ((float)dz);
	float local[12] = { cz * cy * s[0], (cz * sy * sx - sz * cx) * s[1],
		(cz * sy * cx + sz * sx) * s[2], t[0], sz * cy * s[0], (sz * sy * sx + cz * cx) * s[1],
		(sz * sy * cx - cz * sx) * s[2], t[1], -sy * s[0], (cy * sx) * s[1], (cy * cx) * s[2],
		t[2] };
	if (!has_parent)
	{
		memcpy (out, local, sizeof (local));
		return;
	}
	for (int r2 = 0; r2 < 3; r2++)
	{
		for (int c = 0; c < 3; c++)
			out[r2 * 4 + c] = parent[r2 * 4] * local[c] + parent[r2 * 4 + 1] * local[4 + c]
				+ parent[r2 * 4 + 2] * local[8 + c];
		out[r2 * 4 + 3] = parent[r2 * 4] * local[3] + parent[r2 * 4 + 1] * local[7]
			+ parent[r2 * 4 + 2] * local[11] + parent[r2 * 4 + 3];
	}
}

static void pk_xpos (const float m[12], float *x, float *y, float *z)
{
	const float px = *x, py = *y, pz = *z;
	*x = m[0] * px + m[1] * py + m[2] * pz + m[3];
	*y = m[4] * px + m[5] * py + m[6] * pz + m[7];
	*z = m[8] * px + m[9] * py + m[10] * pz + m[11];
}

static void pk_xnrm (const float m[12], float *x, float *y, float *z)
{
	const float px = *x, py = *y, pz = *z;
	*x = m[0] * px + m[1] * py + m[2] * pz;
	*y = m[4] * px + m[5] * py + m[6] * pz;
	*z = m[8] * px + m[9] * py + m[10] * pz;
}

typedef struct
{
	int parent;
	float s[3], r[3], t[3];
	u16 *matis, *shapeis;
	uint npoly;
} pk_joint_t;

static void pk_free_joints (pk_joint_t *joints, uint n)
{
	if (!joints)
		return;
	for (uint i = 0; i < n; i++)
	{
		FREE (joints[i].matis);
		FREE (joints[i].shapeis);
	}
	FREE (joints);
}

model_t *ParsePIKMOD (const u8 *data, size_t size)
{
	if (!IsPIKMOD (data, size))
		return 0;
	pk_chunk_t chunks[PK_MAX_CHUNKS];
	uint nchunks = 0;
	if (!pk_walk (data, (uint)size, chunks, &nchunks))
		return 0;

	const pk_chunk_t *c_pos = pk_find (chunks, nchunks, PKC_POS);
	const pk_chunk_t *c_nrm = pk_find (chunks, nchunks, PKC_NRM);
	const pk_chunk_t *c_col = pk_find (chunks, nchunks, PKC_COL);
	const pk_chunk_t *c_tex = pk_find (chunks, nchunks, PKC_TEX);
	const pk_chunk_t *c_txattr = pk_find (chunks, nchunks, PKC_TXATTR);
	const pk_chunk_t *c_mat = pk_find (chunks, nchunks, PKC_MAT);
	const pk_chunk_t *c_skin = pk_find (chunks, nchunks, PKC_SKIN);
	const pk_chunk_t *c_env = pk_find (chunks, nchunks, PKC_ENV);
	const pk_chunk_t *c_mesh = pk_find (chunks, nchunks, PKC_MESH);
	const pk_chunk_t *c_joint = pk_find (chunks, nchunks, PKC_JOINT);
	const pk_chunk_t *c_jname = pk_find (chunks, nchunks, PKC_JNAME);
	if (!c_pos || !c_mesh || !c_joint)
		return 0;

	uint uv_off[8] = { 0 };
	uint uv_cnt[8] = { 0 };
	uint nuvch = 0;
	for (int t = 0; t < 8; t++)
	{
		const pk_chunk_t *c = pk_find (chunks, nchunks, PKC_UV0 + t);
		if (!c)
			continue;
		if (!pk_pool (data, (uint)size, c, uv_cnt + nuvch, uv_off + nuvch))
			return 0;
		nuvch++;
	}
	uint npos = 0, nnrm = 0, ncol = 0, pos_base = 0, nrm_base = 0, col_base = 0;
	if (!pk_pool (data, (uint)size, c_pos, &npos, &pos_base))
		return 0;
	if (c_nrm && !pk_pool (data, (uint)size, c_nrm, &nnrm, &nrm_base))
		return 0;
	if (c_col && !pk_pool (data, (uint)size, c_col, &ncol, &col_base))
		return 0;
	if (!npos || npos > PK_MAX_VERTS || nnrm > PK_MAX_VERTS || ncol > PK_MAX_VERTS)
		return 0;
	if ((u64)pos_base + (u64)npos * 12 > size
		|| (nnrm && (u64)nrm_base + (u64)nnrm * 12 > size)
		|| (ncol && (u64)col_base + (u64)ncol * 4 > size))
		return 0;
	for (uint t = 0; t < nuvch; t++)
		if (uv_cnt[t] > PK_MAX_VERTS || (u64)uv_off[t] + (u64)uv_cnt[t] * 8 > size)
			return 0;

	model_t *model = CALLOC (1, sizeof (*model));
	if (!model)
		return 0;

	//--- textures ---
	uint ntex = 0;
	if (c_tex)
	{
		if (c_tex->size < 4)
		{
			FreeModel (model);
			return 0;
		}
		ntex = pk_be32 (data + c_tex->off);
		uint tp = (c_tex->off + 4 + 31) & ~31u;
		if (ntex > 1024)
		{
			FreeModel (model);
			return 0;
		}
		for (uint i = 0; i < ntex; i++)
		{
			if ((u64)tp + 32 > size || !IsTXE (data + tp, (uint)(size - tp)))
			{
				FreeModel (model);
				return 0;
			}
			uint w = pk_be16 (data + tp), hh = pk_be16 (data + tp + 2);
			uint need = 0;
			pk_txe_gx (pk_be16 (data + tp + 6), &need, w, hh);
			tp += 32 + need;
		}
	}

	//--- texture attributes: u32 count + 12-byte structs ---
	typedef struct
	{
		u16 tex, pal;
		u8 ws, wt;
	} pk_txattr_t;
	pk_txattr_t *txattr = 0;
	uint ntxattr = 0;
	if (c_txattr)
	{
		if (c_txattr->size < 4)
		{
			FreeModel (model);
			return 0;
		}
		ntxattr = pk_be32 (data + c_txattr->off);
		if (ntxattr > 1024 || (u64)c_txattr->off + 4 + (u64)ntxattr * 12 > size
			|| c_txattr->off + 4 + ntxattr * 12 > c_txattr->off + c_txattr->size)
		{
			FreeModel (model);
			return 0;
		}
		txattr = MALLOC (ntxattr * sizeof (*txattr));
		if (!txattr && ntxattr)
		{
			FreeModel (model);
			return 0;
		}
		for (uint i = 0; i < ntxattr; i++)
		{
			const u8 *ap = data + c_txattr->off + 4 + i * 12;
			txattr[i].tex = pk_be16 (ap);
			txattr[i].pal = pk_be16 (ap + 2);
			txattr[i].ws = ap[4];
			txattr[i].wt = ap[5];
			if (txattr[i].tex >= ntex && ntex)
			{
				FREE (txattr);
				FreeModel (model);
				return 0;
			}
		}
	}

	//--- materials: count + lightCount + lights + materials ---
	typedef struct
	{
		u8 r, g, b, a, flags;
		u16 texattr;
	} pk_mat_t;
	pk_mat_t *mats = 0;
	uint nmats = 0;
	if (c_mat)
	{
		if (c_mat->size < 24)
		{
			FREE (txattr);
			FreeModel (model);
			return 0;
		}
		nmats = pk_be32 (data + c_mat->off);
		const uint nlights = pk_be32 (data + c_mat->off + 4);
		if (nmats > 1024 || nlights > 256)
		{
			FREE (txattr);
			FreeModel (model);
			return 0;
		}
		uint mp = c_mat->off + 8 + 16;
		for (uint i = 0; i < nlights; i++)
		{
			// 3x MaterialColor(18) + 16 + u32 count + count*32
			if ((u64)mp + 3 * 18 + 16 + 4 > size)
			{
				FREE (txattr);
				FreeModel (model);
				return 0;
			}
			mp += 3 * 18 + 16;
			const uint lc = pk_be32 (data + mp);
			mp += 4;
			if (lc > 1024 || (u64)mp + (u64)lc * 32 > size)
			{
				FREE (txattr);
				FreeModel (model);
				return 0;
			}
			mp += lc * 32;
		}
		mats = CALLOC (nmats ? nmats : 1, sizeof (*mats));
		if (!mats)
		{
			FREE (txattr);
			FreeModel (model);
			return 0;
		}
		for (uint i = 0; i < nmats; i++)
		{
			if ((u64)mp + 12 > size)
			{
				FREE (txattr);
				FREE (mats);
				FreeModel (model);
				return 0;
			}
			const u8 *mpp = data + mp;
			const int attr = (int8_t)mpp[7];
			mats[i].texattr = attr < 0 ? 0xffff : (u16)attr;
			mats[i].r = mpp[8];
			mats[i].g = mpp[9];
			mats[i].b = mpp[10];
			mats[i].a = mpp[11];
			mats[i].flags = mpp[2];
			const uint tev = mpp[3];
			uint adv = 12;
			if (tev)
			{
				if ((u64)mp + 12 + 64 + 8 > size)
				{
					FREE (txattr);
					FREE (mats);
					FreeModel (model);
					return 0;
				}
				const uint nst = pk_be32 (data + mp + 12 + 64);
				if (nst > 32 || (u64)mp + 12 + 64 + 8 + (u64)nst * 68 > size)
				{
					FREE (txattr);
					FREE (mats);
					FreeModel (model);
					return 0;
				}
				adv = 12 + 64 + 8 + nst * 68;
			}
			mp += adv;
		}
	}
	model->num_materials = nmats ? nmats : 1;
	model->materials = CALLOC (model->num_materials, sizeof (*model->materials));
	if (!model->materials)
	{
		FREE (txattr);
		FREE (mats);
		FreeModel (model);
		return 0;
	}
	for (size_t i = 0; i < model->num_materials; i++)
	{
		material_t *m = model->materials + i;
		snprintf (m->name, sizeof (m->name), "Material%u", (uint)i);
		if (i < nmats)
		{
			m->diffuse[0] = mats[i].r / 255.0f;
			m->diffuse[1] = mats[i].g / 255.0f;
			m->diffuse[2] = mats[i].b / 255.0f;
			m->diffuse[3] = mats[i].a / 255.0f;
			m->has_alpha = (mats[i].flags == 2);
			if (mats[i].texattr != 0xffff && mats[i].texattr < ntxattr)
			{
				const int k = m->num_textures++;
				snprintf (m->textures[k], sizeof (m->textures[k]), "Texture%u",
					txattr[mats[i].texattr].tex);
				const u8 wu = txattr[mats[i].texattr].ws, wv = txattr[mats[i].texattr].wt;
				m->wrap_s[k] = wu == 0 ? 0 : wu == 2 ? 2 : 1;
				m->wrap_t[k] = wv == 0 ? 0 : wv == 2 ? 2 : 1;
				m->min_filter[k] = m->mag_filter[k] = 1;
			}
		}
		else
		{
			m->diffuse[0] = m->diffuse[1] = m->diffuse[2] = m->diffuse[3] = 1.0f;
		}
	}
	FREE (mats);

	//--- skinning indices: u32 count + u16s, rigid until 0xFFFF ---
	u16 *rigid = 0, *smooth = 0;
	uint nrigid = 0, nsmooth = 0;
	if (c_skin)
	{
		if (c_skin->size < 4)
		{
			FREE (txattr);
			FreeModel (model);
			return 0;
		}
		const uint n = pk_be32 (data + c_skin->off);
		if (n > 8192 || (u64)c_skin->off + 4 + (u64)n * 2 > size
			|| c_skin->off + 4 + n * 2 > c_skin->off + c_skin->size)
		{
			FREE (txattr);
			FreeModel (model);
			return 0;
		}
		rigid = MALLOC ((n + 1) * sizeof (*rigid));
		smooth = MALLOC ((n + 1) * sizeof (*smooth));
		if (!rigid || !smooth)
		{
			FREE (rigid);
			FREE (smooth);
			FREE (txattr);
			FreeModel (model);
			return 0;
		}
		bool sep = false;
		for (uint i = 0; i < n; i++)
		{
			const u16 v = pk_be16 (data + c_skin->off + 4 + i * 2);
			if (!sep && v != 0xffff)
				rigid[nrigid++] = v;
			else
			{
				sep = true;
				smooth[nsmooth++] = (u16)(0xffff - v);
			}
		}
	}

	//--- envelopes: u32 count + {u16 n, [u16 joint, f32 w]} ---
	typedef struct
	{
		u16 *joints;
		float *weights;
		uint n;
	} pk_env_t;
	pk_env_t *envs = 0;
	uint nenvs = 0;
	if (c_env)
	{
		if (c_env->size < 4)
		{
			FREE (rigid);
			FREE (smooth);
			FREE (txattr);
			FreeModel (model);
			return 0;
		}
		nenvs = pk_be32 (data + c_env->off);
		if (nenvs > 4096)
		{
			FREE (rigid);
			FREE (smooth);
			FREE (txattr);
			FreeModel (model);
			return 0;
		}
		envs = CALLOC (nenvs ? nenvs : 1, sizeof (*envs));
		if (!envs)
		{
			FREE (rigid);
			FREE (smooth);
			FREE (txattr);
			FreeModel (model);
			return 0;
		}
		uint ep = c_env->off + 4;
		for (uint i = 0; i < nenvs; i++)
		{
			if ((u64)ep + 2 > size || ep + 2 > c_env->off + c_env->size)
			{
				for (uint k = 0; k < i; k++)
				{
					FREE (envs[k].joints);
					FREE (envs[k].weights);
				}
				FREE (envs);
				FREE (rigid);
				FREE (smooth);
				FREE (txattr);
				FreeModel (model);
				return 0;
			}
			const uint cnt = pk_be16 (data + ep);
			ep += 2;
			if (cnt > 32 || (u64)ep + (u64)cnt * 6 > size || ep + cnt * 6 > c_env->off + c_env->size)
			{
				for (uint k = 0; k < i; k++)
				{
					FREE (envs[k].joints);
					FREE (envs[k].weights);
				}
				FREE (envs);
				FREE (rigid);
				FREE (smooth);
				FREE (txattr);
				FreeModel (model);
				return 0;
			}
			envs[i].joints = MALLOC ((cnt ? cnt : 1) * sizeof (*envs[i].joints));
			envs[i].weights = MALLOC ((cnt ? cnt : 1) * sizeof (*envs[i].weights));
			if ((cnt && (!envs[i].joints || !envs[i].weights)))
			{
				FREE (envs[i].joints);
				FREE (envs[i].weights);
				for (uint k = 0; k < i; k++)
				{
					FREE (envs[k].joints);
					FREE (envs[k].weights);
				}
				FREE (envs);
				FREE (rigid);
				FREE (smooth);
				FREE (txattr);
				FreeModel (model);
				return 0;
			}
			envs[i].n = cnt;
			for (uint k = 0; k < cnt; k++)
			{
				envs[i].joints[k] = pk_be16 (data + ep);
				envs[i].weights[k] = pk_bef32 (data + ep + 2);
				ep += 6;
			}
		}
	}

	//--- joints: u32 count + structs ---
	pk_joint_t *joints = 0;
	uint njoints = 0;
	{
		if (c_joint->size < 4)
		{
			goto jfail;
		}
		njoints = pk_be32 (data + c_joint->off);
		if (!njoints || njoints > 1024)
		{
		jfail:
			for (uint k = 0; k < nenvs; k++)
			{
				FREE (envs[k].joints);
				FREE (envs[k].weights);
			}
			FREE (envs);
			FREE (rigid);
			FREE (smooth);
			FREE (txattr);
			FreeModel (model);
			return 0;
		}
		joints = CALLOC (njoints, sizeof (*joints));
		if (!joints)
		{
			for (uint k = 0; k < nenvs; k++)
			{
				FREE (envs[k].joints);
				FREE (envs[k].weights);
			}
			FREE (envs);
			FREE (rigid);
			FREE (smooth);
			FREE (txattr);
			FreeModel (model);
			return 0;
		}
		uint jp = c_joint->off + 4;
		for (uint i = 0; i < njoints; i++)
		{
			// parent(4) flags(4) bbox(24) radius(4) s/r/t(36) npoly(4)
			if ((u64)jp + 76 > size || jp + 76 > c_joint->off + c_joint->size)
			{
				pk_free_joints (joints, njoints);
				for (uint k = 0; k < nenvs; k++)
				{
					FREE (envs[k].joints);
					FREE (envs[k].weights);
				}
				FREE (envs);
				FREE (rigid);
				FREE (smooth);
				FREE (txattr);
				FreeModel (model);
				return 0;
			}
			joints[i].parent = pk_be32s (data + jp);
			jp += 4 + 4 + 24 + 4;
			for (int k = 0; k < 3; k++)
				joints[i].s[k] = pk_bef32 (data + jp + k * 4);
			for (int k = 0; k < 3; k++)
				joints[i].r[k] = pk_bef32 (data + jp + 12 + k * 4);
			for (int k = 0; k < 3; k++)
			{
				joints[i].t[k] = pk_bef32 (data + jp + 24 + k * 4);
				if (!isfinite (joints[i].s[k]) || !isfinite (joints[i].r[k])
					|| !isfinite (joints[i].t[k]))
				{
					pk_free_joints (joints, njoints);
					for (uint k2 = 0; k2 < nenvs; k2++)
					{
						FREE (envs[k2].joints);
						FREE (envs[k2].weights);
					}
					FREE (envs);
					FREE (rigid);
					FREE (smooth);
					FREE (txattr);
					FreeModel (model);
					return 0;
				}
			}
			jp += 36;
			joints[i].npoly = pk_be32 (data + jp);
			jp += 4;
			if (joints[i].npoly > 256 || (u64)jp + (u64)joints[i].npoly * 4 > size
				|| jp + joints[i].npoly * 4 > c_joint->off + c_joint->size)
			{
				pk_free_joints (joints, njoints);
				for (uint k = 0; k < nenvs; k++)
				{
					FREE (envs[k].joints);
					FREE (envs[k].weights);
				}
				FREE (envs);
				FREE (rigid);
				FREE (smooth);
				FREE (txattr);
				FreeModel (model);
				return 0;
			}
			joints[i].matis = 0;
			joints[i].shapeis = 0;
			if (joints[i].npoly)
			{
				joints[i].matis = MALLOC (joints[i].npoly * sizeof (*joints[i].matis));
				joints[i].shapeis = MALLOC (joints[i].npoly * sizeof (*joints[i].shapeis));
				if (!joints[i].matis || !joints[i].shapeis)
				{
					for (uint q = 0; q <= i; q++)
					{
						FREE (joints[q].matis);
						FREE (joints[q].shapeis);
					}
					pk_free_joints (joints, njoints);
					for (uint k = 0; k < nenvs; k++)
					{
						FREE (envs[k].joints);
						FREE (envs[k].weights);
					}
					FREE (envs);
					FREE (rigid);
					FREE (smooth);
					FREE (txattr);
					FreeModel (model);
					return 0;
				}
				for (uint gg = 0; gg < joints[i].npoly; gg++)
				{
					joints[i].matis[gg] = pk_be16 (data + jp + gg * 4);
					joints[i].shapeis[gg] = pk_be16 (data + jp + gg * 4 + 2);
				}
			}
			jp += joints[i].npoly * 4;
			if (joints[i].parent < -1 || joints[i].parent >= (int)njoints)
			{
				pk_free_joints (joints, njoints);
				for (uint k = 0; k < nenvs; k++)
				{
					FREE (envs[k].joints);
					FREE (envs[k].weights);
				}
				FREE (envs);
				FREE (rigid);
				FREE (smooth);
				FREE (txattr);
				FreeModel (model);
				return 0;
			}
		}
	}
	// joint names (optional): u32 count + zero-terminated strings
	char (*jnames)[64] = 0;
	if (c_jname)
	{
		if (c_jname->size >= 4)
		{
			const uint nn = pk_be32 (data + c_jname->off);
			if (nn == njoints && nn <= 1024)
			{
				jnames = CALLOC (nn ? nn : 1, sizeof (*jnames));
				if (jnames)
				{
					uint sp = c_jname->off + 4;
					bool ok = true;
					for (uint i = 0; i < nn && ok; i++)
					{
						uint len = 0;
						while (sp + len < size && sp + len < c_jname->off + c_jname->size
							&& data[sp + len] && len < 63)
							len++;
						if (sp + len >= size || sp + len >= c_jname->off + c_jname->size)
							ok = false;
						else
						{
							memcpy (jnames[i], data + sp, len);
							sp += len + 1;
						}
					}
					if (!ok)
					{
						FREE (jnames);
						jnames = 0;
					}
				}
			}
		}
	}

	model->num_joints = njoints;
	model->joints = CALLOC (njoints, sizeof (*model->joints));
	float (*worlds)[12] = CALLOC (njoints, sizeof (*worlds));
	if (!model->joints || !worlds)
	{
		FREE (worlds);
		FREE (jnames);
		pk_free_joints (joints, njoints);
		for (uint k = 0; k < nenvs; k++)
		{
			FREE (envs[k].joints);
			FREE (envs[k].weights);
		}
		FREE (envs);
		FREE (rigid);
		FREE (smooth);
		FREE (txattr);
		FreeModel (model);
		return 0;
	}
	for (size_t j = 0; j < njoints; j++)
	{
		joint_t *jt = model->joints + j;
		if (jnames && jnames[j][0])
			snprintf (jt->name, sizeof (jt->name), "%s", jnames[j]);
		else
			snprintf (jt->name, sizeof (jt->name), "Bone%u", (uint)j);
		jt->parent_idx = joints[j].parent;
		jt->translate.x = joints[j].t[0];
		jt->translate.y = joints[j].t[1];
		jt->translate.z = joints[j].t[2];
		jt->rotate.x = joints[j].r[0];
		jt->rotate.y = joints[j].r[1];
		jt->rotate.z = joints[j].r[2];
		jt->scale.x = joints[j].s[0] ? joints[j].s[0] : 1;
		jt->scale.y = joints[j].s[1] ? joints[j].s[1] : 1;
		jt->scale.z = joints[j].s[2] ? joints[j].s[2] : 1;
		float t[3] = { jt->translate.x, jt->translate.y, jt->translate.z };
		float r[3] = { jt->rotate.x, jt->rotate.y, jt->rotate.z };
		float s[3] = { jt->scale.x, jt->scale.y, jt->scale.z };
		if (jt->parent_idx >= 0)
			pk_trs_world (worlds[j], t, r, s, worlds[jt->parent_idx], true);
		else
			pk_trs_world (worlds[j], t, r, s, 0, false);
		memcpy (jt->bind, worlds[j], sizeof (worlds[j]));
	}
	FREE (jnames);

	// node influences: rigid slots + envelope slots
	const uint nslots = nrigid + nsmooth;
	model->num_node_influences = nslots;
	model->node_influences = CALLOC (nslots ? nslots : 1, sizeof (*model->node_influences));
	if (!model->node_influences)
	{
		FREE (worlds);
		pk_free_joints (joints, njoints);
		for (uint k = 0; k < nenvs; k++)
		{
			FREE (envs[k].joints);
			FREE (envs[k].weights);
		}
		FREE (envs);
		FREE (rigid);
		FREE (smooth);
		FREE (txattr);
		FreeModel (model);
		return 0;
	}
	for (uint i = 0; i < nrigid; i++)
	{
		node_influence_t *inf = model->node_influences + i;
		inf->weights = MALLOC (sizeof (*inf->weights));
		if (!inf->weights)
		{
			FREE (worlds);
			pk_free_joints (joints, njoints);
			for (uint k = 0; k < nenvs; k++)
			{
				FREE (envs[k].joints);
				FREE (envs[k].weights);
			}
			FREE (envs);
			FREE (rigid);
			FREE (smooth);
			FREE (txattr);
			FreeModel (model);
			return 0;
		}
		inf->num_weights = 1;
		inf->weights[0].bone_idx = rigid[i] < njoints ? (int)rigid[i] : 0;
		inf->weights[0].weight = 1.0f;
	}
	for (uint i = 0; i < nsmooth; i++)
	{
		node_influence_t *inf = model->node_influences + nrigid + i;
		const uint e = smooth[i] < nenvs ? smooth[i] : 0;
		const uint cnt = smooth[i] < nenvs ? envs[e].n : 0;
		inf->weights = MALLOC ((cnt ? cnt : 1) * sizeof (*inf->weights));
		if (!inf->weights)
		{
			FREE (worlds);
			pk_free_joints (joints, njoints);
			for (uint k = 0; k < nenvs; k++)
			{
				FREE (envs[k].joints);
				FREE (envs[k].weights);
			}
			FREE (envs);
			FREE (rigid);
			FREE (smooth);
			FREE (txattr);
			FreeModel (model);
			return 0;
		}
		inf->num_weights = cnt;
		for (uint k = 0; k < cnt; k++)
		{
			inf->weights[k].bone_idx
				= envs[e].joints[k] < njoints ? (int)envs[e].joints[k] : 0;
			inf->weights[k].weight = envs[e].weights[k];
		}
	}

	//--- meshes: one per (joint, polygon group), sharing shape buffers ---
	if (c_mesh->size < 4)
	{
		FREE (worlds);
		pk_free_joints (joints, njoints);
		for (uint k = 0; k < nenvs; k++)
		{
			FREE (envs[k].joints);
			FREE (envs[k].weights);
		}
		FREE (envs);
		FREE (rigid);
		FREE (smooth);
		FREE (txattr);
		FreeModel (model);
		return 0;
	}
	uint meshp = c_mesh->off;
	const uint nshapes = pk_be32 (data + meshp);
	meshp += 4;
	uint mp_align = (meshp + 31) & ~31u;
	if (nshapes > 1024 || mp_align > size)
	{
		FREE (worlds);
		pk_free_joints (joints, njoints);
		for (uint k = 0; k < nenvs; k++)
		{
			FREE (envs[k].joints);
			FREE (envs[k].weights);
		}
		FREE (envs);
		FREE (rigid);
		FREE (smooth);
		FREE (txattr);
		FreeModel (model);
		return 0;
	}
	meshp = mp_align;
	// mesh count = sum over joints of npoly
	uint nout = 0;
	for (uint j = 0; j < njoints; j++)
		nout += joints[j].npoly;
	if (!nout || nout > 4096)
	{
		FREE (worlds);
		pk_free_joints (joints, njoints);
		for (uint k = 0; k < nenvs; k++)
		{
			FREE (envs[k].joints);
			FREE (envs[k].weights);
		}
		FREE (envs);
		FREE (rigid);
		FREE (smooth);
		FREE (txattr);
		FreeModel (model);
		return 0;
	}
	model->num_meshes = nout;
	model->meshes = CALLOC (nout ? nout : 1, sizeof (*model->meshes));
	if (!model->meshes)
	{
		FREE (worlds);
		pk_free_joints (joints, njoints);
		for (uint k = 0; k < nenvs; k++)
		{
			FREE (envs[k].joints);
			FREE (envs[k].weights);
		}
		FREE (envs);
		FREE (rigid);
		FREE (smooth);
		FREE (txattr);
		FreeModel (model);
		return 0;
	}

	// parse all shapes into soups first
	pk_soup_t *shapes = CALLOC (nshapes ? nshapes : 1, sizeof (*shapes));
	if (!shapes)
	{
		FREE (shapes);
		FREE (worlds);
		pk_free_joints (joints, njoints);
		for (uint k = 0; k < nenvs; k++)
		{
			FREE (envs[k].joints);
			FREE (envs[k].weights);
		}
		FREE (envs);
		FREE (rigid);
		FREE (smooth);
		FREE (txattr);
		FreeModel (model);
		return 0;
	}
	bool ok = true;
	for (uint s = 0; s < nshapes && ok; s++)
	{
		if ((u64)meshp + 12 > size)
		{
			ok = false;
			break;
		}
		const int bone = pk_be32s (data + meshp);
		const int vtxd = pk_be32s (data + meshp + 4);
		uint npack = pk_be32 (data + meshp + 8);
		meshp += 12;
		(void)bone;
		if (npack > 256)
		{
			ok = false;
			break;
		}
		for (uint p = 0; p < npack && ok; p++)
		{
			if ((u64)meshp + 4 > size)
			{
				ok = false;
				break;
			}
			const uint nmat = pk_be32 (data + meshp);
			meshp += 4;
			if (nmat > 10 || (u64)meshp + (u64)nmat * 2 > size)
			{
				ok = false;
				break;
			}
			u16 mindices[10];
			for (uint m2 = 0; m2 < nmat; m2++)
			{
				mindices[m2] = pk_be16 (data + meshp);
				meshp += 2;
			}
			if ((u64)meshp + 4 > size)
			{
				ok = false;
				break;
			}
			const uint ndl = pk_be32 (data + meshp);
			meshp += 4;
			if (!ndl || ndl > 64)
			{
				ok = false;
				break;
			}
			for (uint d = 0; d < ndl && ok; d++)
			{
				if ((u64)meshp + 12 > size)
				{
					ok = false;
					break;
				}
				meshp += 4; // flags
				meshp += 4; // unk1
				const uint dsz = pk_be32 (data + meshp);
				meshp += 4;
				uint dstart = (meshp + 31) & ~31u;
				if ((u64)dstart + dsz > size || dstart < meshp)
				{
					ok = false;
					break;
				}
				// decode one GX display list
				const bool has_pn = (vtxd & 1) != 0;
				const bool has_txm = (vtxd & 2) != 0;
				const bool has_col = (vtxd & 4) != 0;
				uint txbits = (uint)vtxd >> 3;
				uint ntx = 0;
				for (uint t = 0; t < 8; t++)
					if (txbits & (1u << t))
						ntx++;
				const u8 *dp = data + dstart, *dend = dp + dsz;
				while (dp < dend && ok)
				{
					const u8 opc = *dp++;
					if (opc == 0)
						continue;
					if (opc != 0x90 && opc != 0x98 && opc != 0xa0)
					{
						ok = false;
						break;
					}
					if (dp + 2 > dend)
					{
						ok = false;
						break;
					}
					const uint nv = (uint)dp[0] << 8 | dp[1];
					dp += 2;
					if (!nv || nv > 65536)
					{
						ok = false;
						break;
					}
					pk_corner_t *vv = MALLOC (nv * sizeof (*vv));
					if (!vv)
					{
						ok = false;
						break;
					}
					for (uint vi = 0; vi < nv && ok; vi++)
					{
						uint need = 0;
						if (has_pn)
							need += 1;
						if (has_txm)
							need += 1;
						need += 2 + (c_nrm ? 2 : 0) + (has_col ? 2 : 0) + ntx * 2;
						if ((size_t)(dend - dp) < need)
						{
							ok = false;
							break;
						}
						int slot = -1;
						if (has_pn)
							slot = *dp++;
						if (has_txm)
							dp++;
						const int pi = pk_be16s (dp);
						dp += 2;
						int ni = -1, ci = -1, ti = -1;
						if (c_nrm)
						{
							ni = pk_be16s (dp);
							dp += 2;
						}
						if (has_col)
						{
							ci = pk_be16s (dp);
							dp += 2;
						}
						if (ntx)
						{
							ti = pk_be16s (dp);
							dp += 2;
							for (uint t = 1; t < ntx; t++)
								dp += 2;
						}
						if (has_pn && (slot < 0 || (uint)(slot / 3) >= nmat))
						{
							// PNMTXIDX addresses the packet matrix list (/3)
							if (slot < 0 || slot / 3 >= 10)
							{
								ok = false;
								break;
							}
						}
						int node = -1;
						if (has_pn && slot >= 0)
						{
							const uint mi = (uint)slot / 3;
							if (mi >= nmat || mi >= 10)
							{
								ok = false;
								break;
							}
							node = mindices[mi];
						}
						vv[vi].pos = pi;
						vv[vi].nrm = ni;
						vv[vi].col = ci;
						vv[vi].uv = ti;
						vv[vi].slot = node;
					}
					if (ok)
						ok = pk_tris (shapes + s, opc, vv, nv);
					FREE (vv);
				}
				meshp = dstart + dsz;
			}
		}
		// validate shape corners
		if (ok)
			for (size_t ci = 0; ci < shapes[s].num && ok; ci++)
			{
				const pk_corner_t *cn = shapes[s].v + ci;
				if (cn->pos < 0 || (uint)cn->pos >= npos || cn->slot < -1
					|| cn->slot >= (int)nslots
					|| (c_nrm && (cn->nrm < 0 || (uint)cn->nrm >= nnrm))
					|| (c_col && (cn->col < 0 || (uint)cn->col >= ncol))
					|| (nuvch && (cn->uv < 0 || (uint)cn->uv >= uv_cnt[0])))
					ok = false;
			}
	}
	if (!ok)
	{
		for (uint s = 0; s < nshapes; s++)
			FREE (shapes[s].v);
		FREE (shapes);
		FREE (worlds);
		pk_free_joints (joints, njoints);
		for (uint k = 0; k < nenvs; k++)
		{
			FREE (envs[k].joints);
			FREE (envs[k].weights);
		}
		FREE (envs);
		FREE (rigid);
		FREE (smooth);
		FREE (txattr);
		FreeModel (model);
		return 0;
	}

	// emit one mesh per (joint, polygroup), duplicating shape verts
	size_t out_idx = 0;
	for (uint j = 0; j < njoints; j++)
	{
		for (uint g = 0; g < joints[j].npoly; g++)
		{
			// polygroup g of joint j: need its (mat, shape) pair; re-read
			// from the joint chunk (stored sequentially after npoly).
			// We saved only the first pair; re-walk:
			(void)g;
			mesh_t *mesh = model->meshes + out_idx;
			snprintf (mesh->name, sizeof (mesh->name), "Mesh%u", (uint)out_idx);
			const uint mati = joints[j].matis[g], shapei = joints[j].shapeis[g];
			if (shapei >= nshapes || mati >= (uint)model->num_materials)
			{
				for (uint s = 0; s < nshapes; s++)
					FREE (shapes[s].v);
				FREE (shapes);
				FREE (worlds);
				pk_free_joints (joints, njoints);
				for (uint k = 0; k < nenvs; k++)
				{
					FREE (envs[k].joints);
					FREE (envs[k].weights);
				}
				FREE (envs);
				FREE (rigid);
				FREE (smooth);
				FREE (txattr);
				FreeModel (model);
				return 0;
			}
			mesh->material_idx = (int)mati;
			pk_soup_t *soup = shapes + shapei;
			if (!soup->num || soup->num % 3)
			{
				for (uint s = 0; s < nshapes; s++)
					FREE (shapes[s].v);
				FREE (shapes);
				FREE (worlds);
				pk_free_joints (joints, njoints);
				for (uint k = 0; k < nenvs; k++)
				{
					FREE (envs[k].joints);
					FREE (envs[k].weights);
				}
				FREE (envs);
				FREE (rigid);
				FREE (smooth);
				FREE (txattr);
				FreeModel (model);
				return 0;
			}
			mesh->positions = MALLOC (soup->num * sizeof (*mesh->positions));
			mesh->position_node = MALLOC (soup->num * sizeof (*mesh->position_node));
			mesh->normals = c_nrm ? MALLOC (soup->num * sizeof (*mesh->normals)) : 0;
			mesh->colors[0] = c_col ? MALLOC (soup->num * sizeof (*mesh->colors[0])) : 0;
			mesh->texcoords = nuvch ? MALLOC (soup->num * sizeof (*mesh->texcoords)) : 0;
			mesh->vertices = MALLOC (soup->num * sizeof (*mesh->vertices));
			if (!mesh->positions || !mesh->position_node || !mesh->vertices
				|| (c_nrm && !mesh->normals) || (c_col && !mesh->colors[0])
				|| (nuvch && !mesh->texcoords))
			{
				for (uint s = 0; s < nshapes; s++)
					FREE (shapes[s].v);
				FREE (shapes);
				FREE (worlds);
				pk_free_joints (joints, njoints);
				for (uint k = 0; k < nenvs; k++)
				{
					FREE (envs[k].joints);
					FREE (envs[k].weights);
				}
				FREE (envs);
				FREE (rigid);
				FREE (smooth);
				FREE (txattr);
				FreeModel (model);
				return 0;
			}
			size_t npp = 0, nn2 = 0, nc2 = 0, nu2 = 0;
			int *vpos = MALLOC (soup->num * sizeof (*vpos));
			int *vnrm = MALLOC (soup->num * sizeof (*vnrm));
			int *vcol = MALLOC (soup->num * sizeof (*vcol));
			int *vuv = MALLOC (soup->num * sizeof (*vuv));
			if (!vpos || !vnrm || !vcol || !vuv)
			{
				FREE (vpos);
				FREE (vnrm);
				FREE (vcol);
				FREE (vuv);
				for (uint s = 0; s < nshapes; s++)
					FREE (shapes[s].v);
				FREE (shapes);
				FREE (worlds);
				pk_free_joints (joints, njoints);
				for (uint k = 0; k < nenvs; k++)
				{
					FREE (envs[k].joints);
					FREE (envs[k].weights);
				}
				FREE (envs);
				FREE (rigid);
				FREE (smooth);
				FREE (txattr);
				FreeModel (model);
				return 0;
			}
			for (size_t ci = 0; ci < soup->num; ci++)
			{
				const pk_corner_t *cn = soup->v + ci;
				// resolve slot -> joint(s)
				int slot = cn->slot;
				bool is_smooth = nrigid && slot >= (int)nrigid;
				int rjoint = -1;
				if (!is_smooth && slot >= 0 && (uint)slot < nrigid)
					rjoint = rigid[slot];
				// position pool + optional rigid bake
				const u8 *vp = data + pos_base + (uint)cn->pos * 12;
				float x = pk_bef32 (vp), y = pk_bef32 (vp + 4), z = pk_bef32 (vp + 8);
				float nx = 0, ny = 0, nz = 0;
				if (c_nrm && cn->nrm >= 0)
				{
					const u8 *rp = data + nrm_base + (uint)cn->nrm * 12;
					nx = pk_bef32 (rp);
					ny = pk_bef32 (rp + 4);
					nz = pk_bef32 (rp + 8);
				}
				if (!is_smooth && rjoint >= 0 && (uint)rjoint < njoints)
				{
					pk_xpos (worlds[rjoint], &x, &y, &z);
					pk_xnrm (worlds[rjoint], &nx, &ny, &nz);
				}
				size_t f = npp;
				for (size_t k = 0; k < npp; k++)
					if (mesh->positions[k].x == x && mesh->positions[k].y == y
						&& mesh->positions[k].z == z)
					{
						f = k;
						break;
					}
				if (f == npp)
				{
					mesh->positions[npp].x = x;
					mesh->positions[npp].y = y;
					mesh->positions[npp].z = z;
					mesh->position_node[npp] = slot;
					npp++;
				}
				vpos[ci] = (int)f;
				if (c_nrm && cn->nrm >= 0)
				{
					size_t ff = nn2;
					for (size_t k = 0; k < nn2; k++)
						if (mesh->normals[k].x == nx && mesh->normals[k].y == ny
							&& mesh->normals[k].z == nz)
						{
							ff = k;
							break;
						}
					if (ff == nn2)
					{
						mesh->normals[nn2].x = nx;
						mesh->normals[nn2].y = ny;
						mesh->normals[nn2].z = nz;
						nn2++;
					}
					vnrm[ci] = (int)ff;
				}
				else
					vnrm[ci] = -1;
				if (c_col && cn->col >= 0)
				{
					const u8 *cp = data + col_base + (uint)cn->col * 4;
					color4_t cc = { cp[0] / 255.0f, cp[1] / 255.0f, cp[2] / 255.0f,
						cp[3] / 255.0f };
					size_t ff = nc2;
					for (size_t k = 0; k < nc2; k++)
						if (mesh->colors[0][k].r == cc.r && mesh->colors[0][k].g == cc.g
							&& mesh->colors[0][k].b == cc.b && mesh->colors[0][k].a == cc.a)
						{
							ff = k;
							break;
						}
					if (ff == nc2)
						mesh->colors[0][nc2++] = cc;
					vcol[ci] = (int)ff;
				}
				else
					vcol[ci] = -1;
				if (nuvch && cn->uv >= 0 && (uint)cn->uv < uv_cnt[0])
				{
					const u8 *up = data + uv_off[0] + (uint)cn->uv * 8;
					vec2_t tc = { pk_bef32 (up), pk_bef32 (up + 4) };
					size_t ff = nu2;
					for (size_t k = 0; k < nu2; k++)
						if (mesh->texcoords[k].u == tc.u && mesh->texcoords[k].v == tc.v)
						{
							ff = k;
							break;
						}
					if (ff == nu2)
						mesh->texcoords[nu2++] = tc;
					vuv[ci] = (int)ff;
				}
				else
					vuv[ci] = -1;
			}
			mesh->num_positions = npp;
			mesh->num_normals = nn2;
			if (!c_nrm)
			{
				FREE (mesh->normals);
				mesh->normals = 0;
			}
			mesh->num_colors[0] = nc2;
			if (!c_col)
			{
				FREE (mesh->colors[0]);
				mesh->colors[0] = 0;
			}
			mesh->num_texcoords = nu2;
			if (!nuvch)
			{
				FREE (mesh->texcoords);
				mesh->texcoords = 0;
			}
			mesh->num_vertices = soup->num;
			for (size_t ci = 0; ci < soup->num; ci++)
			{
				vertex_t *vv = mesh->vertices + ci;
				vv->position_idx = vpos[ci];
				vv->normal_idx = vnrm[ci];
				vv->tangent_idx = -1;
				vv->texcoord_idx = vuv[ci];
				vv->matrix_idx = -1;
				vv->color_idx[0] = vcol[ci];
				vv->color_idx[1] = -1;
				for (int k = 0; k < 7; k++)
					vv->extra_texcoord_idx[k] = -1;
			}
			FREE (vpos);
			FREE (vnrm);
			FREE (vcol);
			FREE (vuv);
			out_idx++;
		}
	}
	for (uint s = 0; s < nshapes; s++)
		FREE (shapes[s].v);
	FREE (shapes);
	FREE (worlds);
	pk_free_joints (joints, njoints);
	for (uint k = 0; k < nenvs; k++)
	{
		FREE (envs[k].joints);
		FREE (envs[k].weights);
	}
	FREE (envs);
	FREE (rigid);
	FREE (smooth);
	FREE (txattr);
	return model;
}

enumError DecodePIKMOD (const u8 *data, uint size, ccp out_path)
{
	if (!IsPIKMOD (data, size))
		return ERR_NOTHING_TO_DO;
	model_t *model = ParsePIKMOD (data, size);
	if (!model)
		return ERR_NOTHING_TO_DO;

	// sibling PNGs for embedded TXE textures
	pk_chunk_t chunks[PK_MAX_CHUNKS];
	uint nchunks = 0;
	if (pk_walk (data, size, chunks, &nchunks))
	{
		const pk_chunk_t *c_tex = pk_find (chunks, nchunks, PKC_TEX);
		if (c_tex && c_tex->size >= 4)
		{
			const uint ntex = pk_be32 (data + c_tex->off);
			uint tp = (c_tex->off + 4 + 31) & ~31u;
			for (uint i = 0; i < ntex; i++)
			{
				u8 *rgba = 0;
				uint w = 0, h = 0;
				if (!DecodeTXE_RGBA (&rgba, &w, &h, data + tp, (uint)(size - tp)))
				{
					char path[PATH_MAX], name[64];
					snprintf (name, sizeof (name), "Texture%u.png", i);
					ccp slash = strrchr (out_path, '/');
					const uint dlen = slash ? (uint)(slash - out_path + 1) : 0;
					if (dlen + strlen (name) + 1 < sizeof (path))
					{
						memcpy (path, out_path, dlen);
						strcpy (path + dlen, name);
						SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true);
					}
					else
						FREE (rgba);
				}
				if (IsTXE (data + tp, (uint)(size - tp)))
				{
					uint tw = pk_be16 (data + tp), th = pk_be16 (data + tp + 2);
					uint need = 0;
					pk_txe_gx (pk_be16 (data + tp + 6), &need, tw, th);
					tp += 32 + need;
				}
				else
					break;
			}
		}
	}

	const int rc = ExportModelToGLB (model, out_path);
	FreeModel (model);
	return rc == 0 ? ERR_OK : ERR_CANT_CREATE;
}

// ---- encoder (canonical chunks, rigid + smooth via envelopes) ----

enumError EncodePIKMOD (const model_t *model, u8 **out, uint *out_size)
{
	if (!out || !out_size || !model || !model->num_meshes || !model->num_joints)
		return ERR_INVALID_DATA;
	const size_t nm = model->num_meshes, nj = model->num_joints;
	if (nm > 1024 || nj > 1024)
		return ERR_INVALID_DATA;

	// worlds for rigid baking
	float (*worlds)[12] = CALLOC (nj, sizeof (*worlds));
	if (!worlds)
		return ERR_OUT_OF_MEMORY;
	for (size_t j = 0; j < nj; j++)
	{
		const joint_t *jt = model->joints + j;
		float s[3] = { jt->scale.x ? jt->scale.x : 1, jt->scale.y ? jt->scale.y : 1,
			jt->scale.z ? jt->scale.z : 1 };
		float r[3] = { jt->rotate.x, jt->rotate.y, jt->rotate.z };
		float t[3] = { jt->translate.x, jt->translate.y, jt->translate.z };
		if (jt->parent_idx >= 0 && (size_t)jt->parent_idx < nj)
			pk_trs_world (worlds[j], t, r, s, worlds[jt->parent_idx], true);
		else
			pk_trs_world (worlds[j], t, r, s, 0, false);
	}
	float (*iworlds)[12] = CALLOC (nj, sizeof (*iworlds));
	if (!iworlds)
	{
		FREE (worlds);
		return ERR_OUT_OF_MEMORY;
	}
	for (size_t j = 0; j < nj; j++)
	{
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
		float *o = iworlds[j];
		o[0] = (m[5] * m[10] - m[6] * m[9]) * d;
		o[1] = (m[2] * m[9] - m[1] * m[10]) * d;
		o[2] = (m[1] * m[6] - m[2] * m[5]) * d;
		o[4] = (m[6] * m[8] - m[4] * m[10]) * d;
		o[5] = (m[0] * m[10] - m[2] * m[8]) * d;
		o[6] = (m[2] * m[4] - m[0] * m[6]) * d;
		o[8] = (m[4] * m[9] - m[5] * m[8]) * d;
		o[9] = (m[1] * m[8] - m[0] * m[9]) * d;
		o[10] = (m[0] * m[5] - m[1] * m[4]) * d;
		o[3] = -(o[0] * m[3] + o[1] * m[7] + o[2] * m[11]);
		o[7] = -(o[4] * m[3] + o[5] * m[7] + o[6] * m[11]);
		o[11] = -(o[8] * m[3] + o[9] * m[7] + o[10] * m[11]);
	}

	// pools (file space: un-bake rigid verts by inverse bind)
	pk_f3_t *pospool = 0, *nrmpool = 0;
	float (*uvpool)[2] = 0;
	size_t npos = 0, cpos = 0, nnrm = 0, cnrm = 0, nuv = 0, cuv = 0;
	const bool has_n = true, has_uv = true;

	// per-mesh display lists (single packet + single DL each)
	typedef struct
	{
		u8 *blob;
		uint len;
		u16 slot;
	} pk_dl_t;
	pk_dl_t *dls = CALLOC (nm, sizeof (*dls));
	if (!dls)
	{
		FREE (worlds);
		FREE (iworlds);
		return ERR_OUT_OF_MEMORY;
	}
	for (size_t m = 0; m < nm; m++)
	{
		const mesh_t *mesh = model->meshes + m;
		if (!mesh->num_vertices || mesh->num_vertices % 3)
		{
			FREE (worlds);
			FREE (iworlds);
			FREE (pospool);
			FREE (nrmpool);
			FREE (uvpool);
			for (size_t k = 0; k < m; k++)
				FREE (dls[k].blob);
			FREE (dls);
			return ERR_INVALID_DATA;
		}
		int dom = 0;
		{
			size_t *votes = CALLOC (nj, sizeof (*votes));
			if (!votes)
			{
				FREE (worlds);
				FREE (iworlds);
				FREE (pospool);
				FREE (nrmpool);
				FREE (uvpool);
				for (size_t k = 0; k < m; k++)
					FREE (dls[k].blob);
				FREE (dls);
				return ERR_OUT_OF_MEMORY;
			}
			for (size_t ci = 0; ci < mesh->num_vertices; ci++)
			{
				const int pi = mesh->vertices[ci].position_idx;
				int node = (pi >= 0 && (size_t)pi < mesh->num_positions)
					? mesh->position_node[pi]
					: 0;
				if (node < 0 || (size_t)node >= nj)
					node = 0;
				votes[node]++;
			}
			for (size_t k = 1; k < nj; k++)
				if (votes[k] > votes[dom])
					dom = (int)k;
			FREE (votes);
		}
		u8 *blob = MALLOC (3 + mesh->num_vertices * 8);
		if (!blob)
		{
			FREE (worlds);
			FREE (iworlds);
			FREE (pospool);
			FREE (nrmpool);
			FREE (uvpool);
			for (size_t k = 0; k < m; k++)
				FREE (dls[k].blob);
			FREE (dls);
			return ERR_OUT_OF_MEMORY;
		}
		u8 *bp = blob;
		*bp++ = 0x90;
		pk_wr16 (bp, (u16)mesh->num_vertices);
		bp += 2;
		for (size_t ci = 0; ci < mesh->num_vertices; ci++)
		{
			const vertex_t *vv = mesh->vertices + ci;
			const int pi = vv->position_idx;
			float x = mesh->positions[pi].x, y = mesh->positions[pi].y, z = mesh->positions[pi].z;
			pk_xpos (iworlds[dom], &x, &y, &z);
			size_t f = npos;
			for (size_t k = 0; k < npos; k++)
				if (pospool[k].x == x && pospool[k].y == y && pospool[k].z == z)
				{
					f = k;
					break;
				}
			if (f == npos)
			{
				if (npos >= cpos)
				{
					const size_t nc = cpos ? cpos * 2 : 1024;
					pk_f3_t *nn = REALLOC (pospool, nc * sizeof (*nn));
					if (!nn)
					{
						FREE (blob);
						FREE (worlds);
						FREE (iworlds);
						FREE (pospool);
						FREE (nrmpool);
						FREE (uvpool);
						for (size_t k = 0; k < m; k++)
							FREE (dls[k].blob);
						FREE (dls);
						return ERR_OUT_OF_MEMORY;
					}
					pospool = nn;
					cpos = nc;
				}
				pospool[npos].x = x;
				pospool[npos].y = y;
				pospool[npos].z = z;
				npos++;
			}
			float nx = 0, ny = 0, nz = 0;
			if (vv->normal_idx >= 0 && mesh->normals)
			{
				nx = mesh->normals[vv->normal_idx].x;
				ny = mesh->normals[vv->normal_idx].y;
				nz = mesh->normals[vv->normal_idx].z;
				pk_xnrm (iworlds[dom], &nx, &ny, &nz);
			}
			size_t fn = nnrm;
			for (size_t k = 0; k < nnrm; k++)
				if (nrmpool[k].x == nx && nrmpool[k].y == ny && nrmpool[k].z == nz)
				{
					fn = k;
					break;
				}
			if (fn == nnrm)
			{
				if (nnrm >= cnrm)
				{
					const size_t nc = cnrm ? cnrm * 2 : 1024;
					pk_f3_t *nn = REALLOC (nrmpool, nc * sizeof (*nn));
					if (!nn)
					{
						FREE (blob);
						FREE (worlds);
						FREE (iworlds);
						FREE (pospool);
						FREE (nrmpool);
						FREE (uvpool);
						for (size_t k = 0; k < m; k++)
							FREE (dls[k].blob);
						FREE (dls);
						return ERR_OUT_OF_MEMORY;
					}
					nrmpool = nn;
					cnrm = nc;
				}
				nrmpool[nnrm].x = nx;
				nrmpool[nnrm].y = ny;
				nrmpool[nnrm].z = nz;
				nnrm++;
			}
			float uu = 0, vv2 = 0;
			if (vv->texcoord_idx >= 0 && mesh->texcoords)
			{
				uu = mesh->texcoords[vv->texcoord_idx].u;
				vv2 = mesh->texcoords[vv->texcoord_idx].v;
			}
			size_t ft = nuv;
			for (size_t k = 0; k < nuv; k++)
				if (uvpool[k][0] == uu && uvpool[k][1] == vv2)
				{
					ft = k;
					break;
				}
			if (ft == nuv)
			{
				if (nuv >= cuv)
				{
					const size_t nc = cuv ? cuv * 2 : 1024;
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
							FREE (dls[k].blob);
						FREE (dls);
						return ERR_OUT_OF_MEMORY;
					}
					uvpool = nn;
					cuv = nc;
				}
				uvpool[nuv][0] = uu;
				uvpool[nuv][1] = vv2;
				nuv++;
			}
			*bp++ = (u8)(0); // PNMTXIDX slot 0 (single rigid matrix)
			pk_wr16 (bp, (u16)f);
			bp += 2;
			pk_wr16 (bp, (u16)fn);
			bp += 2;
			pk_wr16 (bp, (u16)ft);
			bp += 2;
		}
		(void)has_n;
		(void)has_uv;
		dls[m].blob = blob;
		dls[m].len = (uint)(bp - blob);
		dls[m].slot = (u16)dom;
	}
	if (npos > 65535 || nnrm > 65535 || nuv > 65535)
	{
		FREE (worlds);
		FREE (iworlds);
		FREE (pospool);
		FREE (nrmpool);
		FREE (uvpool);
		for (size_t k = 0; k < nm; k++)
			FREE (dls[k].blob);
		FREE (dls);
		return ERR_INVALID_DATA;
	}

	// envelope table: rigid joints only (one single-bone envelope each is
	// unnecessary: rigid slots address joints directly)
	const uint nrigid = (uint)nj;
	// textures: from model images (PNG IHDR dims, zero-filled CMPR)
	const uint ntex = (uint)model->num_images;
	uint *tex_w = 0, *tex_h = 0;
	if (ntex)
	{
		tex_w = MALLOC (ntex * sizeof (*tex_w));
		tex_h = MALLOC (ntex * sizeof (*tex_h));
		if (!tex_w || !tex_h)
		{
			FREE (tex_w);
			FREE (tex_h);
			FREE (worlds);
			FREE (iworlds);
			FREE (pospool);
			FREE (nrmpool);
			FREE (uvpool);
			for (size_t k = 0; k < nm; k++)
				FREE (dls[k].blob);
			FREE (dls);
			return ERR_OUT_OF_MEMORY;
		}
		for (uint i = 0; i < ntex; i++)
		{
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
		}
	}

	// chunk sizes. Chunk i starts at coff[i] = sum(8 + pad32) before it,
	// hence coff[i] is always 8*i mod 32 and each payload base (coff+8)
	// is 8*(i+1) mod 32; internal align32 pads below use those true bases
	// so the size fields land exactly (see pk_walk).
	uint pos_sz = 16 + (uint)npos * 12; // POS is chunk 1: base 16 mod 32
	uint nrm_sz = 8 + (uint)nnrm * 12; // NRM is chunk 2: base 24 mod 32
	uint uv_sz = 32 + (uint)nuv * 8; // UV0 is chunk 3: base 0 mod 32
	uint tex_sz = 0;
	uint tex_payload = 0;
	for (uint i = 0; i < ntex; i++)
	{
		uint need = 0;
		pk_txe_gx (1, &need, tex_w[i], tex_h[i]);
		tex_payload += 32 + need;
	}
	tex_sz += 24 + tex_payload; // TEX is chunk 4: base 8 mod 32
	uint txa_sz = 4 + (ntex ? ntex : 1) * 12;
	uint mat_sz = 4 + 4 + 16 + (uint)(model->num_materials ? model->num_materials : 1) * 12;
	uint skin_sz = 4 + (nrigid + 1) * 2;
	uint env_sz = 4;
	// Chunk payload sizes for the fixed chunks (mesh/joint/jname below).
	// Chunk i starts at coff[i], 32-aligned; payload base is coff[i]+8.
	// coff[i] mod 32 is fully determined: coff[0]=0 and each step adds
	// 8 + pad32, i.e. 8 mod 32.
	uint presz[9] = { 32, pos_sz, nrm_sz, uv_sz, tex_sz, txa_sz, mat_sz, skin_sz, env_sz };
	uint preoff[10];
	preoff[0] = 0;
	for (int i = 0; i < 9; i++)
		preoff[i + 1] = (preoff[i] + 8 + ((presz[i] + 31) & ~31u) + 31) & ~31u;
	const uint mesh_pay = preoff[9] + 8;
	const uint mesh_mod = mesh_pay & 31u;
	// mesh payload size, simulating the writer's alignment exactly:
	// count at +0, shapes at align32(+4), each display list blob at
	// align32 after its 12-byte head (all relative to mesh_pay).
	uint mesh_list = 4;
	mesh_list = ((mesh_list + mesh_mod + 31) & ~31u) - mesh_mod;
	for (size_t m = 0; m < nm; m++)
	{
		mesh_list += 12 + 4 + 2 + 4;
		mesh_list += 12;
		mesh_list = ((mesh_list + mesh_mod + 31) & ~31u) - mesh_mod;
		mesh_list += dls[m].len;
	}
	uint joint_sz = 4;
	for (size_t j = 0; j < nj; j++)
		joint_sz += 76 + 4;
	uint jname_sz = 4;
	for (size_t j = 0; j < nj; j++)
		jname_sz += (uint)strlen (model->joints[j].name) + 1;

	uint csize[12];
	const int cops[12]
		= { PKC_HEADER, PKC_POS, PKC_NRM, PKC_UV0, PKC_TEX, PKC_TXATTR, PKC_MAT, PKC_SKIN,
			PKC_ENV, PKC_MESH, PKC_JOINT, PKC_JNAME };
	csize[0] = 32;
	csize[1] = pos_sz;
	csize[2] = nrm_sz;
	csize[3] = uv_sz;
	csize[4] = tex_sz;
	csize[5] = txa_sz;
	csize[6] = mat_sz;
	csize[7] = skin_sz;
	csize[8] = env_sz;
	csize[9] = mesh_list;
	csize[10] = joint_sz;
	csize[11] = jname_sz;
	uint total = 0;
	{
		uint tcur = 0;
		for (int i = 0; i < 12; i++)
		{
			tcur = (tcur + 31) & ~31u;
			tcur += 8 + ((csize[i] + 31) & ~31u);
		}
		tcur = (tcur + 31) & ~31u;
		total = tcur + 8; // aligned EOF record
	}

	u8 *buf = CALLOC (1, total ? total : 1);
	if (!buf)
	{
		FREE (tex_w);
		FREE (tex_h);
		FREE (worlds);
		FREE (iworlds);
		FREE (pospool);
		FREE (nrmpool);
		FREE (uvpool);
		for (size_t k = 0; k < nm; k++)
			FREE (dls[k].blob);
		FREE (dls);
		return ERR_OUT_OF_MEMORY;
	}
	uint cur = 0;
	uint coff[12];
	for (int i = 0; i < 12; i++)
	{
		cur = (cur + 31) & ~31u;
		coff[i] = cur;
		pk_wr32 (buf + cur, (u32)cops[i]);
		pk_wr32 (buf + cur + 4, (csize[i] + 31) & ~31u);
		cur += 8;
		uint pay = coff[i] + 8;
		switch (cops[i])
		{
			case PKC_HEADER:
				memset (buf + pay, 0, 24);
				pk_wr16 (buf + pay + 24, 2023);
				buf[pay + 26] = 1;
				buf[pay + 27] = 1;
				pk_wr32 (buf + pay + 28, 0);
				break;
			case PKC_POS:
				pk_wr32 (buf + pay, (uint)npos);
				pay = (pay + 31) & ~31u;
				for (size_t k = 0; k < npos; k++)
				{
					pk_wrf32 (buf + pay + k * 12, pospool[k].x);
					pk_wrf32 (buf + pay + k * 12 + 4, pospool[k].y);
					pk_wrf32 (buf + pay + k * 12 + 8, pospool[k].z);
				}
				break;
			case PKC_NRM:
				pk_wr32 (buf + pay, (uint)nnrm);
				pay = (pay + 31) & ~31u;
				for (size_t k = 0; k < nnrm; k++)
				{
					pk_wrf32 (buf + pay + k * 12, nrmpool[k].x);
					pk_wrf32 (buf + pay + k * 12 + 4, nrmpool[k].y);
					pk_wrf32 (buf + pay + k * 12 + 8, nrmpool[k].z);
				}
				break;
			case PKC_UV0:
				pk_wr32 (buf + pay, (uint)nuv);
				pay = (pay + 31) & ~31u;
				for (size_t k = 0; k < nuv; k++)
				{
					pk_wrf32 (buf + pay + k * 8, uvpool[k][0]);
					pk_wrf32 (buf + pay + k * 8 + 4, uvpool[k][1]);
				}
				break;
			case PKC_TEX:
			{
				pk_wr32 (buf + pay, ntex);
				uint tp2 = (pay + 4 + 31) & ~31u;
				for (uint ti = 0; ti < ntex; ti++)
				{
					uint need = 0;
					pk_txe_gx (1, &need, tex_w[ti], tex_h[ti]);
					pk_wr16 (buf + tp2, (u16)tex_w[ti]);
					pk_wr16 (buf + tp2 + 2, (u16)tex_h[ti]);
					pk_wr16 (buf + tp2 + 4, 0);
					pk_wr16 (buf + tp2 + 6, 1); // CMPR
					pk_wr32 (buf + tp2 + 8, need);
					tp2 += 32 + need; // pixels stay zero (see header note)
				}
				break;
			}
			case PKC_TXATTR:
			{
				const uint na = ntex ? ntex : 1;
				pk_wr32 (buf + pay, na);
				for (uint ti = 0; ti < na; ti++)
				{
					u8 *ap = buf + pay + 4 + ti * 12;
					pk_wr16 (ap, ti < ntex ? (u16)ti : 0);
					pk_wr16 (ap + 2, 0);
					ap[4] = 1;
					ap[5] = 1;
					pk_wr16 (ap + 6, 0);
					pk_wr32 (ap + 8, 0);
				}
				break;
			}
			case PKC_MAT:
			{
				const uint nmt = model->num_materials ? (uint)model->num_materials : 1;
				pk_wr32 (buf + pay, nmt);
				pk_wr32 (buf + pay + 4, 0); // no lights
				memset (buf + pay + 8, 0, 16);
				uint mp2 = pay + 24;
				for (uint mi = 0; mi < nmt; mi++)
				{
					// 12-byte record: unk u16, flags u8, tev u8, texIdx s16,
					// pad u8, attrIdx s8, diffuse RGBA8
					const material_t *mt = model->num_materials > mi ? model->materials + mi : 0;
					pk_wr16 (buf + mp2, 0);
					buf[mp2 + 2] = mt && mt->has_alpha ? 2 : 0;
					buf[mp2 + 3] = 0; // no tev extras
					pk_wr16 (buf + mp2 + 4, 0); // textureIndex (unused)
					buf[mp2 + 6] = 0;
					buf[mp2 + 7] = mt && mt->num_textures > 0 ? 0 : (u8)0xff;
					if (mt)
					{
						buf[mp2 + 8] = (u8)(mt->diffuse[0] * 255.0f);
						buf[mp2 + 9] = (u8)(mt->diffuse[1] * 255.0f);
						buf[mp2 + 10] = (u8)(mt->diffuse[2] * 255.0f);
						buf[mp2 + 11] = (u8)(mt->diffuse[3] * 255.0f);
					}
					else
					{
						buf[mp2 + 8] = buf[mp2 + 9] = buf[mp2 + 10] = buf[mp2 + 11] = 255;
					}
					mp2 += 12;
				}
				break;
			}
			case PKC_SKIN:
			{
				pk_wr32 (buf + pay, nrigid + 1);
				for (uint ri = 0; ri < nrigid; ri++)
					pk_wr16 (buf + pay + 4 + ri * 2, (u16)ri);
				pk_wr16 (buf + pay + 4 + nrigid * 2, 0xffff);
				break;
			}
			case PKC_ENV:
				pk_wr32 (buf + pay, 0);
				break;
			case PKC_MESH:
			{
				pk_wr32 (buf + pay, (uint)nm);
				uint sp = (pay + 4 + 31) & ~31u;
				for (size_t mi = 0; mi < nm; mi++)
				{
					pk_wr32 (buf + sp, 0); // bone (unused for skinning)
					// vtxDesc: PNMTXIDX + normal + uv0
					pk_wr32 (buf + sp + 4, 1 | (1 << 3));
					pk_wr32 (buf + sp + 8, 1); // one packet
					sp += 12;
					pk_wr32 (buf + sp, 1); // one matrix
					pk_wr16 (buf + sp + 4, dls[mi].slot);
					sp += 6;
					pk_wr32 (buf + sp, 1); // one display list
					sp += 4;
					memset (buf + sp, 0, 4); // flags
					pk_wr32 (buf + sp + 4, 0); // unk1
					pk_wr32 (buf + sp + 8, dls[mi].len);
					sp += 12;
					sp = (sp + 31) & ~31u;
					memcpy (buf + sp, dls[mi].blob, dls[mi].len);
					sp += dls[mi].len;
				}
				break;
			}
			case PKC_JOINT:
			{
				pk_wr32 (buf + pay, (uint)nj);
				uint jp2 = pay + 4;
				for (size_t ji = 0; ji < nj; ji++)
				{
					const joint_t *jt = model->joints + ji;
					pk_wr32 (buf + jp2, jt->parent_idx >= 0 ? (u32)jt->parent_idx : 0xffffffffu);
					pk_wr32 (buf + jp2 + 4, 0); // flags
					jp2 += 8 + 24 + 4; // bbox + radius (zero)
					pk_wrf32 (buf + jp2, jt->scale.x);
					pk_wrf32 (buf + jp2 + 4, jt->scale.y);
					pk_wrf32 (buf + jp2 + 8, jt->scale.z);
					pk_wrf32 (buf + jp2 + 12, jt->rotate.x);
					pk_wrf32 (buf + jp2 + 16, jt->rotate.y);
					pk_wrf32 (buf + jp2 + 20, jt->rotate.z);
					pk_wrf32 (buf + jp2 + 24, jt->translate.x);
					pk_wrf32 (buf + jp2 + 28, jt->translate.y);
					pk_wrf32 (buf + jp2 + 32, jt->translate.z);
					jp2 += 36;
					pk_wr32 (buf + jp2, 1); // one polygroup
					jp2 += 4;
					pk_wr16 (buf + jp2, (u16)(ji < nm ? ji : 0)); // material
					pk_wr16 (buf + jp2 + 2, (u16)(ji < nm ? ji : 0)); // shape
					jp2 += 4;
				}
				break;
			}
			case PKC_JNAME:
			{
				pk_wr32 (buf + pay, (uint)nj);
				uint sp2 = pay + 4;
				for (size_t ji = 0; ji < nj; ji++)
				{
					const size_t nl = strlen (model->joints[ji].name);
					memcpy (buf + sp2, model->joints[ji].name, nl + 1);
					sp2 += (uint)nl + 1;
				}
				break;
			}
			default:
				break;
		}
		cur = coff[i] + 8 + ((csize[i] + 31) & ~31u);
	}
	cur = (cur + 31) & ~31u;
	pk_wr32 (buf + cur, (u32)(int)PKC_EOF);
	pk_wr32 (buf + cur + 4, 0);
	cur += 8;

	FREE (tex_w);
	FREE (tex_h);
	FREE (worlds);
	FREE (iworlds);
	FREE (pospool);
	FREE (nrmpool);
	FREE (uvpool);
	for (size_t k = 0; k < nm; k++)
		FREE (dls[k].blob);
	FREE (dls);

	*out = buf;
	*out_size = total;
	return ERR_OK;
}

enumError EncodeModelToPIKMOD (const model_t *model, ccp out_path)
{
	u8 *buf = 0;
	uint size = 0;
	enumError err = EncodePIKMOD (model, &buf, &size);
	if (err || !buf)
	{
		FREE (buf);
		return err ? err : ERR_INVALID_DATA;
	}
	File_t F;
	err = CreateFileOpt (&F, true, out_path, false, out_path);
	if (!err && F.f && fwrite (buf, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing Pikmin MOD failed: %s\n", out_path);
	ResetFile (&F, opt_preserve);
	FREE (buf);
	return err;
}

//-----------------------------------------------------------------------------
///////////////		Pikmin ARC/DIR pair			///////////////
//-----------------------------------------------------------------------------

enumError ScanPIKARC (pikarc_entry_t **entries, uint *n_entries, const u8 *dir_data, uint dir_size,
	const u8 *arc_data, uint arc_size)
{
	if (!entries || !n_entries || !dir_data || dir_size < 8)
		return ERR_INVALID_DATA;
	const uint fsize = pk_be32 (dir_data);
	const uint ndirs = pk_be32 (dir_data + 4);
	if (!ndirs || ndirs > 100000 || (u64)8 + (u64)ndirs * 12 > dir_size)
		return ERR_INVALID_DATA;
	(void)fsize;
	pikarc_entry_t *out = CALLOC (ndirs ? ndirs : 1, sizeof (*out));
	if (!out)
		return ERR_OUT_OF_MEMORY;
	for (uint i = 0; i < ndirs; i++)
	{
		const u8 *ep = dir_data + 8 + i * 12;
		const uint off = pk_be32 (ep), sz = pk_be32 (ep + 4), nl = pk_be32 (ep + 8);
		const uint npos = 8 + ndirs * 12 + i * 0; // names live after the table
		(void)npos;
		if (nl > 1024 || (u64)off + sz > arc_size)
		{
			for (uint k = 0; k < i; k++)
				FREE (out[k].name);
			FREE (out);
			return ERR_INVALID_DATA;
		}
		// name table: entries are {off, size, namelen} + names packed after;
		// names are found by scanning: name i starts after previous names.
		// Recompute: names begin at 8 + ndirs*12, each namelen bytes.
		uint name_off = 8 + ndirs * 12;
		for (uint k = 0; k < i; k++)
			name_off += pk_be32 (dir_data + 8 + k * 12 + 8);
		if ((u64)name_off + nl > dir_size)
		{
			for (uint k = 0; k < i; k++)
				FREE (out[k].name);
			FREE (out);
			return ERR_INVALID_DATA;
		}
		char *nm = MALLOC (nl + 1);
		if (!nm)
		{
			for (uint k = 0; k < i; k++)
				FREE (out[k].name);
			FREE (out);
			return ERR_OUT_OF_MEMORY;
		}
		memcpy (nm, dir_data + name_off, nl);
		nm[nl] = 0;
		out[i].name = nm;
		out[i].data = arc_data ? arc_data + off : 0;
		out[i].size = arc_data ? sz : 0;
		if (arc_data && (u64)off + sz > arc_size)
		{
			for (uint k = 0; k <= i; k++)
				FREE (out[k].name);
			FREE (out);
			return ERR_INVALID_DATA;
		}
	}
	*n_entries = ndirs;
	*entries = out;
	return ERR_OK;
}

void FreePIKARC (pikarc_entry_t *entries, uint n_entries)
{
	if (!entries)
		return;
	for (uint i = 0; i < n_entries; i++)
		FREE (entries[i].name);
	FREE (entries);
}

enumError CreatePIKARC (u8 **dir_out, uint *dir_size, u8 **arc_out, uint *arc_size,
	const pikarc_entry_t *entries, uint n_entries)
{
	if (!dir_out || !dir_size || !arc_out || !arc_size || !entries || !n_entries)
		return ERR_INVALID_DATA;
	uint names = 0, payload = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		names += (uint)strlen (entries[i].name);
		payload += entries[i].size;
	}
	const uint dsz = 8 + n_entries * 12 + names;
	const uint asz = payload;
	u8 *dd = CALLOC (1, dsz ? dsz : 1);
	u8 *ad = CALLOC (1, asz ? asz : 1);
	if (!dd || !ad)
	{
		FREE (dd);
		FREE (ad);
		return ERR_OUT_OF_MEMORY;
	}
	pk_wr32 (dd, dsz);
	pk_wr32 (dd + 4, n_entries);
	uint noff = 8 + n_entries * 12, aoff = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		const uint nl = (uint)strlen (entries[i].name);
		pk_wr32 (dd + 8 + i * 12, aoff);
		pk_wr32 (dd + 8 + i * 12 + 4, entries[i].size);
		pk_wr32 (dd + 8 + i * 12 + 8, nl);
		memcpy (dd + noff, entries[i].name, nl);
		noff += nl;
		if (entries[i].size && entries[i].data)
			memcpy (ad + aoff, entries[i].data, entries[i].size);
		aoff += entries[i].size;
	}
	*dir_out = dd;
	*dir_size = dsz;
	*arc_out = ad;
	*arc_size = asz;
	return ERR_OK;
}
