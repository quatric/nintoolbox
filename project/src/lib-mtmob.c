// SPDX-License-Identifier: GPL-2.0+
// Capcom MT Framework Mobile (3DS) + ModelBinary (.mbn). See lib-mtmob.h.

#include "lib-mtmob.h"
#include "lib-nintendo.h"
#include "lib-ctpk.h"
#include "lib-bch.h"
#include "lib-model-glb.h"
#include "lib-szs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <dirent.h>

// ---------------------------------------------------------------------------
// CRC32 exactly as SPICA CRC32Hash.Hash (IEEE table, init 0xFFFFFFFF,
// NO final negation -- HashNegated() is a separate function). Retail MRL
// material hashes and MFX input-layout keys use this value.
// ---------------------------------------------------------------------------

static u32 mt_crc32 (const u8 *d, size_t n)
{
	u32 crc = 0xffffffffu;
	for (size_t i = 0; i < n; i++)
	{
		crc ^= d[i];
		for (int k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
	}
	return crc;
}

static u32 mt_crc32_str (const char *s)
{
	return mt_crc32 ((const u8 *)s, strlen (s));
}

// ---------------------------------------------------------------------------
// bounds-checked cursor (little-endian)
// ---------------------------------------------------------------------------

typedef struct
{
	const u8 *d;
	size_t n;
	size_t p;
	int err;
} mtr_t;

static u8 mtr_u8 (mtr_t *r)
{
	if (r->err || r->p + 1 > r->n)
	{
		r->err = 1;
		return 0;
	}
	return r->d[r->p++];
}

static u16 mtr_u16 (mtr_t *r)
{
	if (r->err || r->p + 2 > r->n)
	{
		r->err = 1;
		return 0;
	}
	u16 v = (u16)r->d[r->p] | (u16)r->d[r->p + 1] << 8;
	r->p += 2;
	return v;
}

static s8 mtr_s8 (mtr_t *r)
{
	return (s8)mtr_u8 (r);
}

static u32 mtr_u32 (mtr_t *r)
{
	if (r->err || r->p + 4 > r->n)
	{
		r->err = 1;
		return 0;
	}
	u32 v = rd_le32 (r->d + r->p);
	r->p += 4;
	return v;
}

static float mtr_f32 (mtr_t *r)
{
	u32 u = mtr_u32 (r);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

static void mtr_skip (mtr_t *r, size_t n)
{
	if (r->err || r->p + n > r->n || r->p + n < r->p)
	{
		r->err = 1;
		return;
	}
	r->p += n;
}

static void mtr_seek (mtr_t *r, size_t p)
{
	if (p > r->n)
	{
		r->err = 1;
		return;
	}
	r->p = p;
}

// ---------------------------------------------------------------------------
// probes
// ---------------------------------------------------------------------------

// The FILETYPE probe is only 0x800 bytes (CHECK_FILE_SIZE): tables that
// run past `size` can only be excused when the probe itself is saturated.
// Full data (decode paths) always validates strictly.
static int mt_truncated (size_t size)
{
	return size >= 0x800;
}

int IsMTMOD (const u8 *data, size_t size)
{
	if (!data || size < 0x74 || memcmp (data, "MOD", 3) || data[3] != 0)
		return 0;
	u32 bones = data[6] | (u32)data[7] << 8;
	u32 meshes = data[8] | (u32)data[9] << 8;
	u32 mats = data[10] | (u32)data[11] << 8;
	if (!meshes || meshes > 4096 || bones > 4096 || mats > 4096)
		return 0;
	u32 meshaddr = rd_le32 (data + 0x34);
	u32 vbuf = rd_le32 (data + 0x38);
	u32 ibuf = rd_le32 (data + 0x3c);
	u32 flen = rd_le32 (data + 0x40);
	if (meshaddr >= size)
		return mt_truncated (size);
	if (!mt_truncated (size))
	{
		if ((u64)meshaddr + (u64)meshes * 0x28 > size || vbuf > size || ibuf > size)
			return 0;
	}
	if (flen && flen != size && !mt_truncated (size))
	{
		// retail MODs record their length; tolerate trailing slop of 16 bytes
		if (flen > size || size - flen > 16)
			return 0;
	}
	return 1;
}

int IsMTTEX (const u8 *data, size_t size)
{
	if (!data || size < 0x10 || memcmp (data, "TEX", 3) || data[3] != 0)
		return 0;
	u32 w0 = rd_le32 (data + 4);
	u32 w1 = rd_le32 (data + 8);
	u32 w2 = rd_le32 (data + 12);
	u32 ver = w0 & 0xfff;
	u32 shift = (w0 >> 24) & 0xf;
	u32 w = ((w1 >> 6) & 0x1fff) << shift;
	u32 h = ((w1 >> 19) & 0x1fff) << shift;
	u32 fmt = (w2 >> 8) & 0xff;
	if (!w || !h || w > 4096 || h > 4096 || shift > 8)
		return 0;
	if (ver > 0x200)
		return 0;
	switch (fmt)
	{
		case 0x03:
		case 0x0b:
		case 0x0c:
		case 0x11:
			break;
		default:
			return 0;
	}
	return 1;
}

int IsMTMRL (const u8 *data, size_t size)
{
	if (!data || size < 0x20 || memcmp (data, "MRL", 3) || data[3] != 0)
		return 0;
	u32 mats = rd_le32 (data + 8);
	u32 luts = rd_le32 (data + 12);
	if (mats > 4096 || luts > 4096)
		return 0;
	u32 lutaddr = rd_le32 (data + (rd_le32 (data + 4) == 0xc ? 0x18 : 0x14));
	u32 mataddr = rd_le32 (data + (rd_le32 (data + 4) == 0xc ? 0x1c : 0x18));
	if (mats && mataddr >= size)
		return mt_truncated (size);
	if (luts && lutaddr >= size && !mt_truncated (size))
		return 0;
	if (mats && !mt_truncated (size) && (u64)mataddr + (u64)mats * 0x3c > size)
		return 0;
	return 1;
}

int IsMTMFX (const u8 *data, size_t size)
{
	if (!data || size < 0x30 || memcmp (data, "MFX", 3) || data[3] != 0)
		return 0;
	// SPICA reads the NUL-terminated magic ("MFX\0") then seeks +8 over an
	// unknown block, so the count/address table starts at absolute +12;
	// be lenient and validate the descriptor table instead.
	if (size < 0x2c)
		return 0;
	u32 ndesc = rd_le32 (data + 12);
	if (ndesc > 4096)
		return 0;
	// descriptor pointer table at absolute +40 (see mt_parse_layouts)
	const u32 fmtaddr = 40;
	if (!ndesc)
		return 1;
	if (fmtaddr >= size || (u64)fmtaddr + (u64)ndesc * 4 > size)
		return mt_truncated (size);
	return 1;
}

// ModelBinary: no magic; validate the descriptor chain structurally.
int IsMBN (const u8 *data, size_t size)
{
	if (!data || size < 0x10)
		return 0;
	u32 type = rd_le32 (data);
	u32 vflags = rd_le32 (data + 8);
	s32 nmesh = (s32)rd_le32 (data + 12);
	if (type > 16 || nmesh <= 0 || nmesh > 4096)
		return 0;
	if (vflags & ~1u)
		return 0;
	mtr_t r = { data, size, 16, 0 };
	int single = (vflags & 1) != 0;
	int builtin = type == 4;
	// attribute name/format value ranges (SPICA MBnAttributeName/Format)
	for (int ph = 0; ph < (single ? 1 : 0); ph++)
	{
		(void)ph;
		u32 na = mtr_u32 (&r);
		if (r.err || na > 32)
			return 0;
		for (u32 a = 0; a < na; a++)
		{
			u32 nm = mtr_u32 (&r);
			u32 fm = mtr_u32 (&r);
			mtr_skip (&r, 4);
			if (r.err || nm > 6 || fm > 3)
				return 0;
		}
		s32 bl = (s32)mtr_u32 (&r);
		if (r.err || bl < 0 || (size_t)bl > size)
			return 0;
		if (builtin)
			mtr_skip (&r, (size_t)(bl + 3) & ~(size_t)3);
		if (r.err)
			return 0;
	}
	for (s32 m = 0; m < nmesh && !r.err; m++)
	{
		s32 nsub = (s32)mtr_u32 (&r);
		if (r.err || nsub <= 0 || nsub > 4096)
			return 0;
		for (s32 s = 0; s < nsub; s++)
		{
			u32 nb = mtr_u32 (&r);
			if (r.err || nb > 256)
				return 0;
			mtr_skip (&r, (size_t)nb * 4);
			u32 np = mtr_u32 (&r);
			if (r.err || np > 3000000)
				return 0;
		}
		if (!single)
		{
			u32 na = mtr_u32 (&r);
			if (r.err || na > 32)
				return 0;
			for (u32 a = 0; a < na; a++)
			{
				u32 nm = mtr_u32 (&r);
				u32 fm = mtr_u32 (&r);
				mtr_skip (&r, 4);
				if (r.err || nm > 6 || fm > 3)
					return 0;
			}
			s32 bl = (s32)mtr_u32 (&r);
			if (r.err || bl < 0 || (size_t)bl > size)
				return 0;
			if (builtin)
				mtr_skip (&r, (size_t)(bl + 3) & ~(size_t)3);
		}
	}
	return !r.err;
}

// ---------------------------------------------------------------------------
// MT TEX -> RGBA8
// ---------------------------------------------------------------------------

enumError DecodeMTTEX_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, size_t size)
{
	if (!dest || !width || !height)
		return EINVAL;
	if (!IsMTTEX (data, size))
		return EINVAL;
	u32 w0 = rd_le32 (data + 4);
	u32 w1 = rd_le32 (data + 8);
	u32 w2 = rd_le32 (data + 12);
	u32 ver = w0 & 0xfff;
	u32 shift = (w0 >> 24) & 0xf;
	u32 w = ((w1 >> 6) & 0x1fff) << shift;
	u32 h = ((w1 >> 19) & 0x1fff) << shift;
	u32 fmt = (w2 >> 8) & 0xff;
	uint pica;
	switch (fmt)
	{
		case 0x03:
			pica = 0;
			break; // RGBA8
		case 0x11:
			pica = 1;
			break; // RGB8
		case 0x0b:
			pica = 12;
			break; // ETC1
		case 0x0c:
			pica = 13;
			break; // ETC1A4
		default:
			return EINVAL;
	}
	size_t head = ver > 0xa3 ? 20 : 16; // magic + 3 words (+ version word)
	if (head >= size)
		return EINVAL;
	return DecodePicaTexture (dest, width, height, data + head, w, h, pica, (uint)(size - head));
}

// ---------------------------------------------------------------------------
// sibling search (SPICA needs the .lfx/.mrl next to the .mod; mirror the
// nusktb sibling lookup in wmdlt.c)
// ---------------------------------------------------------------------------

static int mt_load_sibling (ccp dir, ccp base_noext, ccp ext, u8 **out, size_t *outsz)
{
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%s%s", dir, base_noext, ext);
	if (LoadFileAlloc (path, 0, 0, out, outsz, 0, 0, 0, false))
		return 0;
	return 1;
}

// find "<base>.mrl"/"<base>.lfx"/"<base>.mfx" next to the model; base is the
// model path without its extension. Tries exact stem, then stem stripped at
// the last '.'-before-slash safe point is already handled by caller.
static void mt_split_dirbase (ccp arg, char *dir, size_t dirsz, char *base, size_t basesz)
{
	snprintf (dir, dirsz, "%s", arg);
	char *slash = strrchr (dir, '/');
	if (slash)
	{
		*slash = 0;
		snprintf (base, basesz, "%s", slash + 1);
	}
	else
	{
		snprintf (dir, dirsz, ".");
		snprintf (base, basesz, "%s", arg);
	}
	char *dot = strrchr (base, '.');
	if (dot)
		*dot = 0;
}

// ---------------------------------------------------------------------------
// MFX input layouts: mesh VertexFormatHash -> attribute list
// ---------------------------------------------------------------------------

typedef struct
{
	int pica; // GF_A_* numbering (POS/NRM/TAN/COL/UV0..2/BONE/WEIGHT), -1 skip
	uint fmt; // 1 F32, 3 S16, 4 U16, 5 S16N, 6 U16N, 7 S8, 8 U8, 9 S8N, 10 U8N
	uint elems;
	uint off;
	float scale;
} mt_attr_t;

typedef struct
{
	u32 key;
	mt_attr_t *attrs;
	uint n_attrs;
	uint stride;
} mt_layout_t;

static float mt_attr_scale (uint fmt)
{
	switch (fmt)
	{
		case 5:
			return 1.0f / 32767;
		case 6:
			return 1.0f / 65535;
		case 7:
		case 9:
			return 1.0f / 127;
		case 8:
		case 10:
			return 1.0f / 255;
		default:
			return 1.0f;
	}
}

static uint mt_attr_size (uint fmt)
{
	switch (fmt)
	{
		case 1:
			return 4;
		case 3:
		case 4:
		case 5:
		case 6:
			return 2;
		case 7:
		case 8:
		case 9:
		case 10:
			return 1;
		default:
			return 0;
	}
}

// map lowercased MFX attribute name -> PICA slot (0..8), texcoord index out
static int mt_attr_name (const char *name, uint idx)
{
	if (!strcmp (name, "position"))
		return 0;
	if (!strcmp (name, "normal"))
		return 1;
	if (!strcmp (name, "tangent"))
		return 2;
	if (!strcmp (name, "color"))
		return 3;
	if (!strcmp (name, "joint"))
		return 7;
	if (!strcmp (name, "weight"))
		return 8;
	if (!strcmp (name, "texcoord"))
		return idx < 3 ? 4 + (int)idx : -1;
	return -1;
}

static void mt_tolower (char *d, const char *s, size_t n)
{
	size_t i = 0;
	for (; s[i] && i + 1 < n; i++)
		d[i] = (char)tolower ((u8)s[i]);
	d[i] = 0;
}

// read a strings-heap NUL string (rel offset from heap base)
static void mt_heap_str (const u8 *d, size_t n, size_t heap, u32 rel, char *out, size_t outsz)
{
	if (outsz)
		out[0] = 0;
	if (!out || !outsz || (size_t)heap + rel >= n)
		return;
	size_t p = heap + rel;
	size_t e = p;
	while (e < n && d[e])
		e++;
	size_t L = e - p < outsz - 1 ? e - p : outsz - 1;
	memcpy (out, d + p, L);
	out[L] = 0;
}

// Parse all __InputLayout descriptors. Returns count (0 = none / error).
static mt_layout_t *mt_parse_layouts (const u8 *d, size_t n, uint *n_out)
{
	if (n_out)
		*n_out = 0;
	if (!IsMTMFX (d, n))
		return NULL;
	// magic is NUL-terminated ("MFX\0", 4 bytes); SPICA then skips 8 more
	// bytes, so the count/address table below starts at absolute +12 and
	// the descriptor pointer table sits at absolute +40 (VtxFormatsAddr
	// is the current position, not another indirection).
	u32 ndesc = rd_le32 (d + 12);
	u32 strtab = rd_le32 (d + 32);
	const u32 fmttab = 40;
	if (!ndesc || strtab >= n || fmttab >= n)
		return NULL;
	mt_layout_t *layouts = NULL;
	uint nlay = 0;
	for (u32 i = 0; i < ndesc; i++)
	{
		if ((u64)fmttab + (u64)i * 4 + 4 > n)
			break;
		u32 a = rd_le32 (d + fmttab + i * 4);
		if (!a || (u64)a + 20 > n)
			continue;
		u32 nameoff = rd_le32 (d + a);
		u32 typeoff = rd_le32 (d + a + 4);
		u16 didx = d[a + 14] | (u16)d[a + 15] << 8;
		char tname[128];
		mt_heap_str (d, n, strtab, typeoff, tname, sizeof (tname));
		if (strcmp (tname, "__InputLayout") && strcmp (tname, "__inputlayout"))
			continue;
		char dname[128];
		mt_heap_str (d, n, strtab, nameoff, dname, sizeof (dname));
		u32 key = (mt_crc32_str (dname) << 12) | didx;
		// attribute group at mapAddr
		u32 mapaddr = rd_le32 (d + a + 16);
		if (!mapaddr || mapaddr + 8 > n)
			continue;
		u16 craw = d[mapaddr + 1] | (u16)d[mapaddr + 2] << 8;
		uint cnt = (uint)craw >> 4;
		uint stride = (uint)d[mapaddr + 3] * 4;
		if (!cnt || cnt > 16 || !stride || stride > 256)
			continue;
		if ((u64)mapaddr + 8 + (u64)cnt * 8 > n)
			continue;
		mt_layout_t lay;
		memset (&lay, 0, sizeof (lay));
		lay.key = key;
		lay.stride = stride;
		lay.attrs = CALLOC (cnt, sizeof (*lay.attrs));
		if (!lay.attrs)
			continue;
		for (uint k = 0; k < cnt; k++)
		{
			u32 no = rd_le32 (d + mapaddr + 8 + k * 8);
			u32 fm = rd_le32 (d + mapaddr + 12 + k * 8);
			char aname[128], alow[128];
			mt_heap_str (d, n, strtab, no, aname, sizeof (aname));
			mt_tolower (alow, aname, sizeof (alow));
			uint aidx = fm & 0x3f;
			uint afmt = (fm >> 6) & 0x1f;
			uint eraw = (fm >> 11) & 3;
			uint aoff = ((fm >> 24) & 0xff) * 4;
			int pica = mt_attr_name (alow, aidx);
			lay.attrs[lay.n_attrs].pica = pica;
			lay.attrs[lay.n_attrs].fmt = afmt;
			lay.attrs[lay.n_attrs].elems = eraw + 1 > 4 ? 4 : eraw + 1;
			lay.attrs[lay.n_attrs].off = aoff;
			lay.attrs[lay.n_attrs].scale = mt_attr_scale (afmt);
			if (pica >= 0 && mt_attr_size (afmt))
				lay.n_attrs++;
		}
		if (!lay.n_attrs)
		{
			FREE (lay.attrs);
			continue;
		}
		mt_layout_t *nl = REALLOC (layouts, (nlay + 1) * sizeof (*nl));
		if (!nl)
		{
			FREE (lay.attrs);
			break;
		}
		layouts = nl;
		layouts[nlay++] = lay;
	}
	if (n_out)
		*n_out = nlay;
	return layouts;
}

// ---------------------------------------------------------------------------
// MRL: material hash -> base texture name
// ---------------------------------------------------------------------------

typedef struct
{
	u32 hash;
	char tex[64];
} mt_mattex_t;

static mt_mattex_t *mt_parse_mrl (const u8 *d, size_t n, uint *n_out)
{
	if (n_out)
		*n_out = 0;
	if (!IsMTMRL (d, n))
		return NULL;
	u32 ver = rd_le32 (d + 4);
	u32 mats = rd_le32 (d + 8);
	u32 nluts = rd_le32 (d + 12);
	u32 lutaddr = rd_le32 (d + (ver == 0xc ? 0x18 : 0x14));
	u32 mataddr = rd_le32 (d + (ver == 0xc ? 0x1c : 0x18));
	(void)nluts;
	if (!mats)
		return NULL;
	mt_mattex_t *out = CALLOC (mats, sizeof (*out));
	if (!out)
		return NULL;
	uint got = 0;
	for (u32 i = 0; i < mats; i++)
	{
		size_t m = (size_t)mataddr + i * 0x3c;
		if (m + 0x3c > n)
			break;
		u32 hash = rd_le32 (d + m + 4);
		u32 taddr = rd_le32 (d + m + 0x34);
		// texture-desc count lives right after the blend/depth hashes
		u32 tcnt = d[m + (ver < 0x20 ? 0x10 : 0x0c) + 12];
		out[got].hash = hash;
		out[got].tex[0] = 0;
		if (tcnt && tcnt < 64 && taddr && (u64)taddr + (u64)tcnt * 12 <= n)
		{
			for (u32 t = 0; t < tcnt; t++)
			{
				u32 flags = rd_le32 (d + taddr + t * 12);
				// SPICA stores texIdxPlus1 (0 = none); the LUT index is one less
				s32 tidx = (s32)rd_le32 (d + taddr + t * 12 + 4) - 1;
				if ((flags & 0xf) != 3 || tidx < 0)
					continue;
				size_t L = (size_t)lutaddr + (size_t)tidx * 0x4c + 0x0c;
				if (L + 0x40 > n)
					continue;
				// SPICA only takes the BaseMap LUT; without the map-type
				// table we take the first texture entry, which is the
				// diffuse map in retail files.
				if (!out[got].tex[0])
				{
					memcpy (out[got].tex, d + L, 0x3f);
					out[got].tex[0x3f] = 0;
					out[got].tex[strnlen (out[got].tex, 0x3f)] = 0;
				}
			}
		}
		got++;
	}
	if (n_out)
		*n_out = got;
	return out;
}

// ---------------------------------------------------------------------------
// MT MOD -> model_t
// ---------------------------------------------------------------------------

static float mt_read_vert_elem (const u8 *p, uint fmt)
{
	switch (fmt)
	{
		case 7:
			return (float)(s8)p[0];
		case 8:
			return (float)p[0];
		case 3:
		case 5:
		{
			u16 v = (u16)p[0] | (u16)p[1] << 8;
			return (float)(s16)v;
		}
		case 4:
		case 6:
		{
			u16 v = (u16)p[0] | (u16)p[1] << 8;
			return (float)v;
		}
		case 1:
		default:
		{
			u32 u = rd_le32 (p);
			float f;
			memcpy (&f, &u, 4);
			return f;
		}
	}
}

typedef struct
{
	int bones[4];
	float weights[4];
	int n;
} mt_inf_t;

static int mt_inf_add (
	mt_inf_t **tab, size_t *n, size_t *cap, const int *bones, const float *w, int cnt)
{
	for (size_t i = 0; i < *n; i++)
	{
		if ((*tab)[i].n != cnt)
			continue;
		int same = 1;
		for (int k = 0; k < cnt; k++)
			if ((*tab)[i].bones[k] != bones[k] || fabsf ((*tab)[i].weights[k] - w[k]) > 1e-6f)
			{
				same = 0;
				break;
			}
		if (same)
			return (int)i;
	}
	if (*n >= *cap)
	{
		size_t ncap = *cap ? *cap * 2 : 64;
		if (ncap > 65536)
			return -1;
		mt_inf_t *nt = REALLOC (*tab, ncap * sizeof (**tab));
		if (!nt)
			return -1;
		*tab = nt;
		*cap = ncap;
	}
	(*tab)[*n].n = cnt;
	memcpy ((*tab)[*n].bones, bones, sizeof (int) * 4);
	memcpy ((*tab)[*n].weights, w, sizeof (float) * 4);
	return (int)(*n)++;
}

void *ParseMTMOD (const u8 *data, size_t size, ccp sibling_dir)
{
	if (!IsMTMOD (data, size))
		return NULL;
	u32 nbones = data[6] | (u32)data[7] << 8;
	u32 nmeshes = data[8] | (u32)data[9] << 8;
	u32 nmats = data[10] | (u32)data[11] << 8;
	u32 vbuflen = rd_le32 (data + 0x18);
	u32 nbonegroups = rd_le32 (data + 0x24);
	u32 skeladdr = rd_le32 (data + 0x28);
	u32 matnameaddr = rd_le32 (data + 0x30);
	u32 meshaddr = rd_le32 (data + 0x34);
	u32 vbufaddr = rd_le32 (data + 0x38);
	u32 ibufaddr = rd_le32 (data + 0x3c);

	model_t *out = CALLOC (1, sizeof (model_t));
	if (!out)
		return NULL;

	// skeleton: nbones x 0x18, then local/world matrices, 0x100 skip, groups
	if (nbones && skeladdr && skeladdr < size)
	{
		out->joints = CALLOC (nbones, sizeof (joint_t));
		if (!out->joints)
		{
			FREE (out);
			return NULL;
		}
		float (*loc)[16] = NULL;
		if ((u64)skeladdr + (u64)nbones * 0x18 + (u64)nbones * 128 <= size)
		{
			loc = (float (*)[16])CALLOC (nbones, sizeof (*loc));
			for (u32 b = 0; b < nbones && loc; b++)
			{
				size_t bo = (size_t)skeladdr + b * 0x18;
				s8 parent = (s8)data[bo + 1];
				// position vec3 LE floats at +0x0c
				float px = 0, py = 0, pz = 0;
				{
					const u8 *pp = data + bo + 0x0c;
					u32 x = rd_le32 (pp), y = rd_le32 (pp + 4), z = rd_le32 (pp + 8);
					memcpy (&px, &x, 4);
					memcpy (&py, &y, 4);
					memcpy (&pz, &z, 4);
				}
				snprintf (out->joints[b].name, sizeof (out->joints[b].name), "bone_%u", b);
				out->joints[b].parent_idx = parent < 0 ? -1 : parent;
				out->joints[b].translate.x = px;
				out->joints[b].translate.y = py;
				out->joints[b].translate.z = pz;
				out->joints[b].scale.x = out->joints[b].scale.y = out->joints[b].scale.z = 1.0f;
			}
			// local matrices follow the bone records (column-major on disk)
			size_t mo = (size_t)skeladdr + (size_t)nbones * 0x18;
			for (u32 b = 0; b < nbones && loc; b++)
			{
				for (int k = 0; k < 16; k++)
				{
					u32 u = rd_le32 (data + mo + (size_t)b * 64 + k * 4);
					memcpy (&loc[b][k], &u, 4);
				}
			}
			FREE (loc);
		}
		else
		{
			for (u32 b = 0; b < nbones; b++)
			{
				size_t bo = (size_t)skeladdr + b * 0x18;
				if (bo + 0x18 > size)
					break;
				s8 parent = (s8)data[bo + 1];
				snprintf (out->joints[b].name, sizeof (out->joints[b].name), "bone_%u", b);
				out->joints[b].parent_idx = parent < 0 ? -1 : parent;
				out->joints[b].scale.x = out->joints[b].scale.y = out->joints[b].scale.z = 1.0f;
			}
		}
		out->num_joints = nbones;
		ComputeModelTRSBinds (out);
	}

	// bone-index groups (after skeleton matrices + 0x100 unknown block)
	u8 *bonegroups = NULL; // flattened: nbonegroups x 24 payload + counts
	u32 *groupcnt = NULL;
	if (nbonegroups && nbonegroups < 4096 && nbones)
	{
		size_t go = (size_t)skeladdr + (size_t)nbones * 0x18 + (size_t)nbones * 128 + 0x100;
		if (go < size)
		{
			bonegroups = CALLOC (nbonegroups * 24, 1);
			groupcnt = CALLOC (nbonegroups, sizeof (*groupcnt));
			for (u32 g = 0; g < nbonegroups && bonegroups && groupcnt; g++)
			{
				if (go + 4 > size)
					break;
				s32 c = (s32)rd_le32 (data + go);
				go += 4;
				if (c < 0 || c > 24)
					break;
				groupcnt[g] = (u32)c;
				if (go + (size_t)c > size)
					break;
				memcpy (bonegroups + g * 24, data + go, (size_t)c);
				go += 24; // stride is 4 + 0x18 payload (SPICA seeks 0x18-count)
			}
		}
	}

	// material names
	char (*matnames)[128] = NULL;
	if (nmats && matnameaddr < size)
	{
		matnames = CALLOC (nmats, 128);
		for (u32 i = 0; i < nmats && matnames; i++)
		{
			size_t o = (size_t)matnameaddr + i * 0x80;
			if (o + 0x80 > size)
				break;
			memcpy (matnames[i], data + o, 127);
			matnames[i][127] = 0;
			matnames[i][strnlen (matnames[i], 127)] = 0;
		}
	}

	// sibling .mrl (material->texture) and .mfx/.lfx (vertex layouts)
	mt_mattex_t *mtex = NULL;
	uint n_mtex = 0;
	mt_layout_t *layouts = NULL;
	uint n_layouts = 0;
	u8 *sib = NULL;
	size_t sibsz = 0;
	if (sibling_dir)
	{
		DIR *dp = opendir (sibling_dir);
		if (dp)
		{
			// load every .mrl/.mfx/.lfx in the directory (normally one set)
			for (struct dirent *de; (de = readdir (dp));)
			{
				size_t L = strlen (de->d_name);
				char path[PATH_MAX];
				if (L > 4
					&& (!strcasecmp (de->d_name + L - 4, ".mrl")
						|| !strcasecmp (de->d_name + L - 4, ".mfx")
						|| !strcasecmp (de->d_name + L - 4, ".lfx")))
				{
					snprintf (path, sizeof (path), "%s/%s", sibling_dir, de->d_name);
					u8 *d2 = NULL;
					size_t s2 = 0;
					if (LoadFileAlloc (path, 0, 0, &d2, &s2, 0, 0, 0, false))
						continue;
					if (IsMTMRL (d2, s2) && !mtex)
						mtex = mt_parse_mrl (d2, s2, &n_mtex);
					else if (IsMTMFX (d2, s2) && !layouts)
						layouts = mt_parse_layouts (d2, s2, &n_layouts);
					FREE (d2);
				}
			}
			closedir (dp);
		}
	}
	(void)sib;
	(void)sibsz;
	(void)mt_load_sibling;
	(void)mt_split_dirbase;

	if (nmats)
	{
		out->materials = CALLOC (nmats, sizeof (material_t));
		if (out->materials)
		{
			out->num_materials = nmats;
			for (u32 i = 0; i < nmats; i++)
			{
				material_t *m = &out->materials[i];
				if (matnames && matnames[i][0])
					snprintf (m->name, sizeof (m->name), "%s", matnames[i]);
				else
					snprintf (m->name, sizeof (m->name), "mat_%u", i);
				m->diffuse[0] = m->diffuse[1] = m->diffuse[2] = 0.8f;
				m->diffuse[3] = 1.0f;
				if (mtex && matnames && matnames[i][0])
				{
					u32 h = mt_crc32_str (matnames[i]);
					for (uint t = 0; t < n_mtex; t++)
						if (mtex[t].hash == h && mtex[t].tex[0])
						{
							snprintf (m->textures[0], sizeof (m->textures[0]), "%s", mtex[t].tex);
							m->texture_coord[0] = 0;
							m->wrap_s[0] = m->wrap_t[0] = 1;
							m->min_filter[0] = m->mag_filter[0] = 1;
							m->num_textures = 1;
							break;
						}
				}
			}
		}
	}

	// meshes
	mt_inf_t *inf = NULL;
	size_t n_inf = 0, cap_inf = 0;
	float bbmin[3] = { 0, 0, 0 }, bbmax[3] = { 0, 0, 0 };
	if (size >= 0x74)
	{
		u32 u;
		memcpy (&u, data + 0x54, 4);
		u = rd_le32 ((const u8 *)&u);
		memcpy (&bbmin[0], &u, 4);
		for (int k = 0; k < 3; k++)
		{
			u = rd_le32 (data + 0x54 + k * 4);
			memcpy (&bbmin[k], &u, 4);
			u = rd_le32 (data + 0x64 + k * 4);
			memcpy (&bbmax[k], &u, 4);
		}
	}
	for (u32 mi = 0; mi < nmeshes; mi++)
	{
		size_t mo = (size_t)meshaddr + mi * 0x28;
		if (mo + 0x28 > size)
			break;
		u32 vertcount = data[mo + 2] | (u32)data[mo + 3] << 8;
		u32 matmesh = rd_le32 (data + mo + 4);
		int render = (int)(s8)(matmesh >> 24);
		u32 matidx = (matmesh >> 12) & 0xfff;
		u8 stride = data[mo + 0x0a];
		u32 vertidx = rd_le32 (data + mo + 0x0c);
		u32 vertoff = rd_le32 (data + mo + 0x10);
		u32 fmthash = rd_le32 (data + mo + 0x14);
		u32 idxidx = rd_le32 (data + mo + 0x18);
		u32 idxcount = rd_le32 (data + mo + 0x1c);
		u8 bonecnt = data[mo + 0x24];
		u8 boneidx = data[mo + 0x25];
		(void)bonecnt;
		if (render != -1)
			continue; // SPICA ToH3D only renders RenderType -1
		if (!vertcount || vertcount > 1000000 || !stride || stride > 128)
			continue;
		if (!idxcount || idxcount < 3 || idxcount > 3000000)
			continue;
		size_t voff = (size_t)vbufaddr + vertoff + (size_t)vertidx * stride;
		size_t ioff = (size_t)ibufaddr + (size_t)idxidx * 2;
		if (voff + (size_t)vertcount * stride > size || vbuflen < (size_t)vertcount * stride)
		{
			if (voff + (size_t)vertcount * stride > size)
				continue;
		}
		if (ioff + (size_t)idxcount * 2 > size)
			continue;

		// destripe triangle strip -> list (degenerate-safe)
		u32 *idx = CALLOC (idxcount, sizeof (*idx));
		if (!idx)
			continue;
		for (u32 k = 0; k < idxcount; k++)
		{
			u16 v = data[ioff + k * 2] | (u16)data[ioff + k * 2 + 1] << 8;
			idx[k] = v >= vertidx ? (u32)(v - vertidx) : 0;
		}
		u32 *tri = CALLOC (idxcount, sizeof (*tri));
		if (!tri)
		{
			FREE (idx);
			continue;
		}
		size_t ntri = 0;
		for (u32 k = 0; k + 2 < idxcount; k++)
		{
			u32 a = idx[k], b = idx[k + 1], c = idx[k + 2];
			if (a >= vertcount || b >= vertcount || c >= vertcount || a == b || b == c || a == c)
				continue;
			if (k & 1)
			{
				u32 t = b;
				b = c;
				c = t;
			}
			tri[ntri++] = a;
			tri[ntri++] = b;
			tri[ntri++] = c;
		}
		FREE (idx);
		if (!ntri)
		{
			FREE (tri);
			continue;
		}

		// vertex layout: MFX match first, else validated float fallback
		const mt_layout_t *lay = NULL;
		for (uint L = 0; L < n_layouts; L++)
			if (layouts[L].key == fmthash && layouts[L].stride == stride)
			{
				lay = &layouts[L];
				break;
			}
		int use_fallback = 0;
		mt_attr_t fb[3];
		if (!lay)
		{
			// common mobile layout: pos(3f) + normal(3f) + uv(2f) = 32B
			if (stride == 32)
			{
				memset (fb, 0, sizeof (fb));
				fb[0].pica = 0;
				fb[0].fmt = 1;
				fb[0].elems = 3;
				fb[0].off = 0;
				fb[0].scale = 1;
				fb[1].pica = 1;
				fb[1].fmt = 1;
				fb[1].elems = 3;
				fb[1].off = 12;
				fb[1].scale = 1;
				fb[2].pica = 4;
				fb[2].fmt = 1;
				fb[2].elems = 2;
				fb[2].off = 24;
				fb[2].scale = 1;
				// validate: first vertex position inside header bbox
				u32 x0 = rd_le32 (data + voff), y0 = rd_le32 (data + voff + 4),
					z0 = rd_le32 (data + voff + 8);
				float fx, fy, fz;
				memcpy (&fx, &x0, 4);
				memcpy (&fy, &y0, 4);
				memcpy (&fz, &z0, 4);
				if (fx >= bbmin[0] - 1 && fx <= bbmax[0] + 1 && fy >= bbmin[1] - 1
					&& fy <= bbmax[1] + 1 && fz >= bbmin[2] - 1 && fz <= bbmax[2] + 1)
					use_fallback = 1;
			}
			if (!use_fallback)
			{
				FREE (tri);
				continue;
			}
		}
		const mt_attr_t *attrs = lay ? lay->attrs : fb;
		uint n_attrs = lay ? lay->n_attrs : 3;

		mesh_t *mesh = NULL;
		{
			mesh_t *nm = REALLOC (out->meshes, (out->num_meshes + 1) * sizeof (*nm));
			if (!nm)
			{
				FREE (tri);
				continue;
			}
			out->meshes = nm;
			mesh = &out->meshes[out->num_meshes];
			memset (mesh, 0, sizeof (*mesh));
		}
		snprintf (mesh->name, sizeof (mesh->name), "mesh_%u", mi);
		mesh->material_idx = (matidx < nmats) ? (int)matidx : -1;
		mesh->positions = CALLOC (ntri, sizeof (*mesh->positions));
		mesh->normals = CALLOC (ntri, sizeof (*mesh->normals));
		mesh->texcoords = CALLOC (ntri, sizeof (*mesh->texcoords));
		mesh->vertices = CALLOC (ntri, sizeof (*mesh->vertices));
		mesh->position_node = CALLOC (ntri, sizeof (*mesh->position_node));
		if (!mesh->positions || !mesh->normals || !mesh->texcoords || !mesh->vertices
			|| !mesh->position_node)
		{
			FREE (mesh->positions);
			FREE (mesh->normals);
			FREE (mesh->texcoords);
			FREE (mesh->vertices);
			FREE (mesh->position_node);
			memset (mesh, 0, sizeof (*mesh));
			FREE (tri);
			continue;
		}
		for (size_t k = 0; k < ntri; k++)
			mesh->position_node[k] = -1;
		size_t n = 0;
		for (size_t k = 0; k < ntri; k++)
		{
			const u8 *vp = data + voff + (size_t)tri[k] * stride;
			float pos[3] = { 0, 0, 0 }, nrm[3] = { 0, 0, 0 }, uv[2] = { 0, 0 };
			int ji[4] = { -1, -1, -1, -1 };
			float jw[4] = { 0, 0, 0, 0 };
			int nj = 0;
			for (uint a = 0; a < n_attrs; a++)
			{
				const mt_attr_t *at = &attrs[a];
				uint sz = mt_attr_size (at->fmt);
				if (!sz || at->off + at->elems * sz > stride)
					continue;
				float v[4] = { 0, 0, 0, 0 };
				for (uint e = 0; e < at->elems && e < 4; e++)
					v[e] = mt_read_vert_elem (vp + at->off + e * sz, at->fmt) * at->scale;
				switch (at->pica)
				{
					case 0:
						pos[0] = v[0];
						pos[1] = v[1];
						pos[2] = v[2];
						break;
					case 1:
						nrm[0] = v[0];
						nrm[1] = v[1];
						nrm[2] = v[2];
						break;
					case 4:
						uv[0] = v[0];
						uv[1] = v[1];
						break;
					case 7:
						for (uint e = 0; e < at->elems && nj < 4; e++)
						{
							int bv = (int)v[e];
							int global = bv;
							if (bonegroups && groupcnt && boneidx < nbonegroups
								&& (u32)bv < groupcnt[boneidx])
								global = bonegroups[boneidx * 24 + bv];
							ji[nj++] = global;
						}
						break;
					case 8:
						for (uint e = 0; e < at->elems && e < 4; e++)
							jw[e] = v[e];
						break;
					default:
						break;
				}
			}
			mesh->positions[n].x = pos[0];
			mesh->positions[n].y = pos[1];
			mesh->positions[n].z = pos[2];
			mesh->normals[n].x = nrm[0];
			mesh->normals[n].y = nrm[1];
			mesh->normals[n].z = nrm[2];
			mesh->texcoords[n].u = uv[0];
			mesh->texcoords[n].v = uv[1];
			if (nj > 0)
			{
				int cb[4] = { -1, -1, -1, -1 };
				float cw[4] = { 0, 0, 0, 0 };
				int cn = 0;
				for (int j = 0; j < nj && cn < 4; j++)
				{
					if (ji[j] < 0 || (u32)ji[j] >= nbones)
						continue;
					float w = jw[j] > 0 ? jw[j] : (j == 0 ? 1.0f : 0.0f);
					if (w <= 0)
						continue;
					cb[cn] = ji[j];
					cw[cn] = w;
					cn++;
				}
				if (cn > 1)
				{
					float s = 0;
					for (int j = 0; j < cn; j++)
						s += cw[j];
					if (s > 0)
						for (int j = 0; j < cn; j++)
							cw[j] /= s;
				}
				if (cn > 0)
				{
					int ii = mt_inf_add (&inf, &n_inf, &cap_inf, cb, cw, cn);
					if (ii >= 0)
						mesh->position_node[n] = ii;
				}
			}
			mesh->vertices[n].position_idx = (int)n;
			mesh->vertices[n].normal_idx = (int)n;
			mesh->vertices[n].texcoord_idx = (int)n;
			mesh->vertices[n].tangent_idx = -1;
			mesh->vertices[n].color_idx[0] = mesh->vertices[n].color_idx[1] = -1;
			for (int t = 0; t < 7; t++)
				mesh->vertices[n].extra_texcoord_idx[t] = -1;
			n++;
		}
		FREE (tri);
		if (!n)
		{
			FREE (mesh->positions);
			FREE (mesh->normals);
			FREE (mesh->texcoords);
			FREE (mesh->vertices);
			FREE (mesh->position_node);
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}
		mesh->num_positions = mesh->num_normals = mesh->num_texcoords = mesh->num_vertices = n;
		out->num_meshes++;
	}

	FREE (matnames);
	FREE (mtex);
	for (uint L = 0; L < n_layouts; L++)
		FREE (layouts[L].attrs);
	FREE (layouts);
	FREE (bonegroups);
	FREE (groupcnt);
	if (n_inf)
	{
		out->node_influences = CALLOC (n_inf, sizeof (node_influence_t));
		if (out->node_influences)
		{
			out->num_node_influences = n_inf;
			for (size_t i = 0; i < n_inf; i++)
			{
				node_influence_t *d = &out->node_influences[i];
				d->weights = CALLOC ((size_t)inf[i].n, sizeof (*d->weights));
				if (!d->weights)
					continue;
				d->num_weights = (size_t)inf[i].n;
				for (int k = 0; k < inf[i].n; k++)
				{
					d->weights[k].bone_idx = inf[i].bones[k];
					d->weights[k].weight = inf[i].weights[k];
				}
			}
		}
	}
	FREE (inf);
	if (!out->num_meshes && !out->num_joints)
	{
		FreeModel (out);
		return NULL;
	}
	return out;
}

// ---------------------------------------------------------------------------
// MBN -> model_t (companion BCH supplies skeleton + materials)
// ---------------------------------------------------------------------------

typedef struct
{
	uint name; // MBnAttributeName 0..6
	uint fmt; // MBnAttributeFormat 0..3 (Float/Ubyte/Byte/Short)
	float scale;
} mbn_attr_t;

typedef struct
{
	mbn_attr_t *attrs;
	uint n_attrs;
	u8 *buf;
	u32 len;
	uint stride;
} mbn_vdesc_t;

typedef struct
{
	u16 *bones;
	uint n_bones;
	u16 *idx;
	u32 n_idx;
} mbn_idesc_t;

// stride rule from SPICA VerticesConverter usage in MBn
static uint mbn_stride (const mbn_attr_t *attrs, uint n)
{
	uint stride = 0;
	for (uint a = 0; a < n; a++)
	{
		uint fmt = attrs[a].fmt, el;
		switch (attrs[a].name)
		{
			case 0:
			case 1:
				el = 3;
				break;
			case 2:
				el = 4;
				break;
			default:
				el = 2;
				break;
		}
		// PICA order in SPICA MBnAttributeFormat: 0 Float, 1 Ubyte, 2 Byte, 3 Short
		uint pica = fmt == 0 ? 3 : fmt == 1 ? 1 : fmt == 2 ? 0 : 2;
		if (pica != 1 && pica != 0)
			stride += stride & 1;
		uint sz = el << (pica == 2 ? 1 : pica == 3 ? 2 : 0);
		stride += sz;
	}
	stride += stride & 1;
	return stride;
}

static float mbn_read (const u8 *p, uint pica_fmt)
{
	switch (pica_fmt)
	{
		case 0:
			return (float)(s8)p[0];
		case 1:
			return (float)p[0];
		case 2:
		{
			u16 v = (u16)p[0] | (u16)p[1] << 8;
			return (float)(s16)v;
		}
		default:
		{
			u32 u = rd_le32 (p);
			float f;
			memcpy (&f, &u, 4);
			return f;
		}
	}
}

void *ParseMBN (const u8 *data, size_t size, const u8 *bch_data, size_t bch_size)
{
	if (!IsMBN (data, size) || !bch_data || !bch_size)
		return NULL;
	// base scene first: skeleton, materials and mesh names/order
	model_t *out = ParseBCH (bch_data, (uint)(bch_size > 0xffffffffu ? 0xffffffffu : bch_size));
	if (!out || !out->num_meshes)
	{
		FreeModel (out);
		return NULL;
	}
	mtr_t r = { data, size, 0, 0 };
	u32 type = mtr_u32 (&r);
	mtr_u32 (&r); // mesh flags
	u32 vflags = mtr_u32 (&r);
	s32 nmesh = (s32)mtr_u32 (&r);
	int single = (vflags & 1) != 0;
	int builtin = type == 4;
	if (r.err || nmesh <= 0)
	{
		FreeModel (out);
		return NULL;
	}

	mbn_vdesc_t shared;
	memset (&shared, 0, sizeof (shared));
	if (single)
	{
		u32 na = mtr_u32 (&r);
		if (r.err || na > 32)
		{
			FreeModel (out);
			return NULL;
		}
		shared.attrs = CALLOC (na ? na : 1, sizeof (*shared.attrs));
		if (!shared.attrs)
		{
			FreeModel (out);
			return NULL;
		}
		for (u32 a = 0; a < na; a++)
		{
			shared.attrs[a].name = mtr_u32 (&r);
			shared.attrs[a].fmt = mtr_u32 (&r);
			shared.attrs[a].scale = mtr_f32 (&r);
			if (r.err)
			{
				FREE (shared.attrs);
				FreeModel (out);
				return NULL;
			}
		}
		shared.n_attrs = na;
		shared.len = mtr_u32 (&r);
		if (r.err || shared.len > size)
		{
			FREE (shared.attrs);
			FreeModel (out);
			return NULL;
		}
		if (builtin)
		{
			if (r.p + shared.len > size)
			{
				FREE (shared.attrs);
				FreeModel (out);
				return NULL;
			}
			shared.buf = (u8 *)data + r.p;
			r.p += (shared.len + 3) & ~(size_t)3;
		}
		shared.stride = mbn_stride (shared.attrs, shared.n_attrs);
	}

	// trailing external buffers (align 0x20 before each, 4 after)
	size_t tail = r.p;
	if (!builtin)
		tail = (tail + 0x1f) & ~(size_t)0x1f;

	for (s32 m = 0; m < nmesh && !r.err; m++)
	{
		s32 nsub = (s32)mtr_u32 (&r);
		if (r.err || nsub <= 0 || nsub > 1024)
			break;
		mbn_idesc_t *subs = CALLOC ((size_t)nsub, sizeof (*subs));
		if (!subs)
			break;
		int ok = 1;
		for (s32 s = 0; s < nsub; s++)
		{
			u32 nb = mtr_u32 (&r);
			if (r.err || nb > 256)
			{
				ok = 0;
				break;
			}
			subs[s].bones = CALLOC (nb ? nb : 1, sizeof (u16));
			if (!subs[s].bones)
			{
				ok = 0;
				break;
			}
			for (u32 b = 0; b < nb; b++)
			{
				u32 v = mtr_u32 (&r);
				subs[s].bones[b] = v > 0xffff ? 0xffff : (u16)v;
			}
			subs[s].n_bones = nb;
			u32 np = mtr_u32 (&r);
			if (r.err || np > 3000000)
			{
				ok = 0;
				break;
			}
			subs[s].n_idx = np;
		}
		mbn_vdesc_t vd;
		memset (&vd, 0, sizeof (vd));
		if (ok && !single)
		{
			u32 na = mtr_u32 (&r);
			if (r.err || na > 32)
				ok = 0;
			else
			{
				vd.attrs = CALLOC (na ? na : 1, sizeof (*vd.attrs));
				if (!vd.attrs)
					ok = 0;
				else
					for (u32 a = 0; a < na; a++)
					{
						vd.attrs[a].name = mtr_u32 (&r);
						vd.attrs[a].fmt = mtr_u32 (&r);
						vd.attrs[a].scale = mtr_f32 (&r);
					}
				vd.n_attrs = na;
				vd.len = mtr_u32 (&r);
				if (r.err || vd.len > size)
					ok = 0;
				else if (builtin)
				{
					if (r.p + vd.len > size)
						ok = 0;
					else
					{
						vd.buf = (u8 *)data + r.p;
						r.p += (vd.len + 3) & ~(size_t)3;
					}
				}
				vd.stride = mbn_stride (vd.attrs, vd.n_attrs);
			}
		}
		else if (ok)
			vd = shared; // alias (do not free)

		// resolve external buffers
		if (ok && !builtin)
		{
			if (single && m == 0 && !shared.buf)
			{
				if (tail + shared.len > size)
					ok = 0;
				else
				{
					shared.buf = (u8 *)data + tail;
					tail += (shared.len + 3) & ~(size_t)3;
					tail = (tail + 0x1f) & ~(size_t)0x1f;
					vd = shared;
				}
			}
			if (!single && !vd.buf)
			{
				if (tail + vd.len > size)
					ok = 0;
				else
				{
					vd.buf = (u8 *)data + tail;
					tail += (vd.len + 3) & ~(size_t)3;
				}
			}
			for (s32 s = 0; s < nsub && ok; s++)
			{
				tail = (tail + 0x1f) & ~(size_t)0x1f;
				if (tail + (size_t)subs[s].n_idx * 2 > size)
				{
					ok = 0;
					break;
				}
				subs[s].idx = (u16 *)(data + tail);
				tail += ((size_t)subs[s].n_idx * 2 + 3) & ~(size_t)3;
			}
		}
		else if (ok)
		{
			// inline buffers follow the descriptors; indices are u16 lists
			for (s32 s = 0; s < nsub && ok; s++)
			{
				if (r.p + (size_t)subs[s].n_idx * 2 > size)
				{
					ok = 0;
					break;
				}
				subs[s].idx = (u16 *)(data + r.p);
				r.p += ((size_t)subs[s].n_idx * 2 + 3) & ~(size_t)3;
			}
		}

		// apply to base mesh m (in-order pairing)
		if (ok && (size_t)m < out->num_meshes && vd.buf && vd.stride)
		{
			mesh_t *mesh = &out->meshes[m];
			size_t total = 0;
			for (s32 s = 0; s < nsub; s++)
				total += subs[s].n_idx;
			if (total && total < 3000000)
			{
				vec3_t *pos = CALLOC (total, sizeof (*pos));
				vec3_t *nrm = CALLOC (total, sizeof (*nrm));
				vec2_t *uv = CALLOC (total, sizeof (*uv));
				vertex_t *vx = CALLOC (total, sizeof (*vx));
				if (pos && uv && vx)
				{
					size_t n = 0;
					for (s32 s = 0; s < nsub && n < total; s++)
					{
						for (u32 k = 0; k < subs[s].n_idx && n < total; k++)
						{
							u32 vi = subs[s].idx[k];
							if ((size_t)vi * vd.stride + vd.stride > vd.len)
								continue;
							const u8 *vp = vd.buf + (size_t)vi * vd.stride;
							size_t ap = 0;
							float P[3] = { 0, 0, 0 }, N[3] = { 0, 0, 0 }, T[2] = { 0, 0 };
							for (uint a = 0; a < vd.n_attrs; a++)
							{
								uint pica = vd.attrs[a].fmt == 0 ? 3
									: vd.attrs[a].fmt == 1		 ? 1
									: vd.attrs[a].fmt == 2		 ? 0
																 : 2;
								uint el = vd.attrs[a].name <= 1 ? 3 : vd.attrs[a].name == 2 ? 4 : 2;
								if ((pica == 2 || pica == 3) && (ap & 1))
									ap++;
								float v[4] = { 0, 0, 0, 0 };
								for (uint e = 0; e < el && e < 4; e++)
								{
									uint sz = pica == 3 ? 4 : pica == 2 ? 2 : 1;
									if (ap + sz > vd.stride)
										break;
									v[e] = mbn_read (vp + ap, pica) * vd.attrs[a].scale;
									ap += sz;
								}
								if (pica == 2 || pica == 3)
									ap += ap & 1;
								switch (vd.attrs[a].name)
								{
									case 0:
										P[0] = v[0];
										P[1] = v[1];
										P[2] = v[2];
										break;
									case 1:
										N[0] = v[0];
										N[1] = v[1];
										N[2] = v[2];
										break;
									case 3:
										T[0] = v[0];
										T[1] = v[1];
										break;
									default:
										break;
								}
							}
							pos[n].x = P[0];
							pos[n].y = P[1];
							pos[n].z = P[2];
							if (nrm)
							{
								nrm[n].x = N[0];
								nrm[n].y = N[1];
								nrm[n].z = N[2];
							}
							uv[n].u = T[0];
							uv[n].v = T[1];
							vx[n].position_idx = (int)n;
							vx[n].normal_idx = nrm ? (int)n : -1;
							vx[n].texcoord_idx = (int)n;
							vx[n].tangent_idx = -1;
							vx[n].color_idx[0] = vx[n].color_idx[1] = -1;
							for (int t = 0; t < 7; t++)
								vx[n].extra_texcoord_idx[t] = -1;
							n++;
						}
					}
					if (n)
					{
						FREE (mesh->positions);
						FREE (mesh->normals);
						FREE (mesh->texcoords);
						FREE (mesh->vertices);
						mesh->positions = pos;
						mesh->normals = nrm;
						mesh->texcoords = uv;
						mesh->vertices = vx;
						mesh->num_positions = mesh->num_normals = mesh->num_texcoords
							= mesh->num_vertices = n;
					}
					else
					{
						FREE (pos);
						FREE (nrm);
						FREE (uv);
						FREE (vx);
					}
				}
				else
				{
					FREE (pos);
					FREE (nrm);
					FREE (uv);
					FREE (vx);
				}
			}
		}
		for (s32 s = 0; s < nsub; s++)
			FREE (subs[s].bones);
		FREE (subs);
		if (!single)
			FREE (vd.attrs);
		if (r.err)
			break;
	}
	if (single)
		FREE (shared.attrs);
	return out;
}

// ---------------------------------------------------------------------------
// manifests
// ---------------------------------------------------------------------------

enumError DecodeMTMOD_Text (FILE *f, const u8 *data, size_t size)
{
	if (!f)
		return EINVAL;
	if (!IsMTMOD (data, size))
		return EINVAL;
	u32 bones = data[6] | (u32)data[7] << 8;
	u32 meshes = data[8] | (u32)data[9] << 8;
	u32 mats = data[10] | (u32)data[11] << 8;
	fprintf (f, "# MTMOD (Capcom MT Framework Mobile model, SPICA MTModel)\n");
	fprintf (f, "version: %u\nbones: %u\nmeshes: %u\nmaterials: %u\n", data[4] | (u32)data[5] << 8,
		bones, meshes, mats);
	fprintf (f, "total_vertices: %u\ntotal_indices: %u\ntotal_triangles: %u\n",
		rd_le32 (data + 0x0c), rd_le32 (data + 0x10), rd_le32 (data + 0x14));
	u32 matnameaddr = rd_le32 (data + 0x30);
	for (u32 i = 0; i < mats; i++)
	{
		size_t o = (size_t)matnameaddr + i * 0x80;
		if (o + 0x80 > size)
			break;
		char nm[128];
		memcpy (nm, data + o, 127);
		nm[127] = 0;
		fprintf (f, "material[%u]: %s\n", i, nm);
	}
	u32 meshaddr = rd_le32 (data + 0x34);
	for (u32 i = 0; i < meshes; i++)
	{
		size_t mo = (size_t)meshaddr + i * 0x28;
		if (mo + 0x28 > size)
			break;
		u32 vc = data[mo + 2] | (u32)data[mo + 3] << 8;
		u32 mm = rd_le32 (data + mo + 4);
		fprintf (f, "mesh[%u]: verts=%u mat=%u render=%d stride=%u fmt_hash=0x%08x idx=%u\n", i, vc,
			(mm >> 12) & 0xfff, (int)(s8)(mm >> 24), data[mo + 0x0a], rd_le32 (data + mo + 0x14),
			rd_le32 (data + mo + 0x1c));
	}
	return ERR_OK;
}

enumError DecodeMTMRL_Text (FILE *f, const u8 *data, size_t size)
{
	if (!f)
		return EINVAL;
	if (!IsMTMRL (data, size))
		return EINVAL;
	uint n = 0;
	mt_mattex_t *mt = mt_parse_mrl (data, size, &n);
	fprintf (f, "# MTMRL (Capcom MT Framework Mobile materials, SPICA MTMaterials)\n");
	fprintf (f, "version: %u\nmaterials: %u\n", rd_le32 (data + 4), n);
	for (uint i = 0; i < n; i++)
		fprintf (f, "material[%u]: hash=0x%08x diffuse=%s\n", i, mt[i].hash,
			mt[i].tex[0] ? mt[i].tex : "<none>");
	FREE (mt);
	return ERR_OK;
}

enumError DecodeMTMFX_Text (FILE *f, const u8 *data, size_t size)
{
	if (!f)
		return EINVAL;
	if (!IsMTMFX (data, size))
		return EINVAL;
	uint n = 0;
	mt_layout_t *lays = mt_parse_layouts (data, size, &n);
	fprintf (f, "# MTMFX (Capcom MT Framework Mobile shader effects, SPICA MTShaderEffects)\n");
	fprintf (f, "descriptors: %u\ninput_layouts: %u\n", rd_le32 (data + 12), n);
	static const char *pnames[]
		= { "pos", "nrm", "tan", "col", "uv0", "uv1", "uv2", "joint", "weight" };
	for (uint i = 0; i < n; i++)
	{
		fprintf (f, "layout[%u]: key=0x%08x stride=%u attrs=%u\n", i, lays[i].key, lays[i].stride,
			lays[i].n_attrs);
		for (uint a = 0; a < lays[i].n_attrs; a++)
		{
			int p = lays[i].attrs[a].pica;
			fprintf (f, "  attr: %s elems=%u fmt=%u off=%u\n", p >= 0 && p < 9 ? pnames[p] : "?",
				lays[i].attrs[a].elems, lays[i].attrs[a].fmt, lays[i].attrs[a].off);
		}
		FREE (lays[i].attrs);
	}
	FREE (lays);
	return ERR_OK;
}

enumError DecodeMBN_Text (FILE *f, const u8 *data, size_t size)
{
	if (!f)
		return EINVAL;
	if (!IsMBN (data, size))
		return EINVAL;
	u32 type = rd_le32 (data);
	u32 vflags = rd_le32 (data + 8);
	s32 nmesh = (s32)rd_le32 (data + 12);
	fprintf (f, "# MBN (SPICA ModelBinary: replacement vertex/index buffers)\n");
	fprintf (f, "type: %u (%s)\nshared_vertex_desc: %s\nmeshes: %d\n", type,
		type == 4 ? "inline" : "external", (vflags & 1) ? "yes" : "no", nmesh);
	mtr_t r = { data, size, 16, 0 };
	static const char *anames[] = { "pos", "nrm", "col", "uv0", "uv1", "bone", "weight" };
	static const char *afmts[] = { "f32", "u8", "s8", "s16" };
	if (vflags & 1)
	{
		u32 na = mtr_u32 (&r);
		fprintf (f, "shared_attrs: %u\n", na);
		for (u32 a = 0; a < na && !r.err; a++)
		{
			u32 nm = mtr_u32 (&r), fm = mtr_u32 (&r);
			float sc = mtr_f32 (&r);
			fprintf (f, "  attr: %s %s scale=%g\n", nm < 7 ? anames[nm] : "?",
				fm < 4 ? afmts[fm] : "?", sc);
		}
		s32 bl = (s32)mtr_u32 (&r);
		fprintf (f, "shared_buffer: %d bytes\n", bl);
		if (type == 4)
			mtr_skip (&r, (size_t)(bl + 3) & ~(size_t)3);
	}
	for (s32 m = 0; m < nmesh && !r.err; m++)
	{
		s32 nsub = (s32)mtr_u32 (&r);
		if (r.err || nsub <= 0 || nsub > 1024)
			break;
		fprintf (f, "mesh[%d]: submeshes=%d\n", m, nsub);
		for (s32 s = 0; s < nsub && !r.err; s++)
		{
			u32 nb = mtr_u32 (&r);
			if (r.err || nb > 256)
				break;
			fprintf (f, "  sub[%d]: bones=%u indices=", s, nb);
			mtr_skip (&r, (size_t)nb * 4);
			u32 np = mtr_u32 (&r);
			fprintf (f, "%u\n", np);
			if (r.err || np > 3000000)
				break;
		}
		if (!(vflags & 1) && !r.err)
		{
			u32 na = mtr_u32 (&r);
			fprintf (f, "  attrs: %u\n", na);
			for (u32 a = 0; a < na && !r.err; a++)
			{
				u32 nm = mtr_u32 (&r), fm = mtr_u32 (&r);
				float sc = mtr_f32 (&r);
				fprintf (f, "    attr: %s %s scale=%g\n", nm < 7 ? anames[nm] : "?",
					fm < 4 ? afmts[fm] : "?", sc);
			}
			s32 bl = (s32)mtr_u32 (&r);
			fprintf (f, "  buffer: %d bytes\n", bl);
			if (type == 4)
				mtr_skip (&r, (size_t)(bl + 3) & ~(size_t)3);
		}
	}
	return r.err ? EINVAL : ERR_OK;
}
