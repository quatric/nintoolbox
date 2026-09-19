// SPDX-License-Identifier: GPL-2.0+
// Game Freak Nintendo 3DS formats (Pokemon X/Y, ORAS, Sun/Moon era).
//
// Reference: SPICA by gdkchan (https://github.com/gdkchan/SPICA) and the
// 3dsTools CLI wrapper by KillzXGaming
// (https://github.com/KillzXGaming/3dsTools). Magic numbers, header layouts,
// the PICA200 command-buffer vertex decoding, the GFNV1 name hash and the
// float24 fixed-attribute conversion below are ported from that library's
// SPICA/Formats/GFL2, SPICA/Formats/Packages/GFL and SPICA/Formats/GFLX
// trees (GFModel, GFMesh/GFSubMesh, GFBone, GFMaterial/GFTextureCoord,
// GFTexture, GFMotion, GFModelPack, GFPackage, GFLXPack, GF1MotionPack).

#include "lib-gf3ds.h"
#include "lib-archive-util.h"
#include "lib-nintendo.h"
#include "lib-ctpk.h"
#include "lib-model-glb.h"
#include "lib-szs.h"
#include "lz4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ---------------------------------------------------------------------------
// tiny bounds-checked little-endian cursor
// ---------------------------------------------------------------------------

typedef struct
{
	const u8 *d;
	size_t n;
	size_t p;
	int err;
} gfr_t;

static u8 gfr_u8 (gfr_t *r)
{
	if (r->err || r->p + 1 > r->n)
	{
		r->err = 1;
		return 0;
	}
	return r->d[r->p++];
}

static u16 gfr_u16 (gfr_t *r)
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

static u32 gfr_u32 (gfr_t *r)
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

static float gfr_f32 (gfr_t *r)
{
	u32 u = gfr_u32 (r);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

static void gfr_skip (gfr_t *r, size_t n)
{
	if (r->err || r->p + n > r->n || r->p + n < r->p)
	{
		r->err = 1;
		return;
	}
	r->p += n;
}

static void gfr_align16 (gfr_t *r)
{
	if (r->p & 0xf)
		gfr_skip (r, 0x10 - (r->p & 0xf));
}

// ByteLengthString: u8 length + bytes (SPICA ReadByteLengthString).
static size_t gfr_blen_str (gfr_t *r, char *buf, size_t bufsz)
{
	u8 len = gfr_u8 (r);
	if (r->err || r->p + len > r->n)
	{
		r->err = 1;
		return 0;
	}
	size_t copy = len < bufsz - 1 ? len : bufsz - 1;
	memcpy (buf, r->d + r->p, copy);
	buf[copy] = 0;
	r->p += len;
	return copy;
}

// GFSection: 8-byte padded magic + u32 length + u32 padding. On success sets
// *payload to the first payload byte and returns the payload length; the
// cursor is left right after the 16-byte header.
static size_t gfr_section (gfr_t *r, const char *want_magic, size_t *payload)
{
	if (r->err || r->p + 16 > r->n)
	{
		r->err = 1;
		return 0;
	}
	if (want_magic && strncmp ((const char *)r->d + r->p, want_magic, strlen (want_magic)))
	{
		r->err = 1;
		return 0;
	}
	r->p += 8;
	u32 len = gfr_u32 (r);
	gfr_skip (r, 4);
	if (r->err)
		return 0;
	if (payload)
		*payload = r->p;
	if (r->p + len > r->n)
	{
		r->err = 1;
		return 0;
	}
	return len;
}

// ---------------------------------------------------------------------------
// GFNV1 name hash (SPICA/Formats/GFL2/GFNV1.cs): multiply-then-xor over the
// ASCII bytes up to the first NUL, seeded with the FNV prime.
// ---------------------------------------------------------------------------

static u32 gf_nv1 (const char *name)
{
	u32 h = 16777619u;
	for (; *name; name++)
	{
		h *= 16777619u;
		h ^= (u8)*name;
	}
	return h;
}

// ---------------------------------------------------------------------------
// float24 (PICA fixed attributes, SPICA PICAVectorFloat24.GetFloat24)
// ---------------------------------------------------------------------------

static float gf_float24 (u32 v)
{
	u32 f;
	if ((v & 0x7fffff) != 0)
	{
		u32 mant = v & 0xffff;
		u32 exp = ((v >> 16) & 0x7f) + 64;
		u32 sign = (v >> 23) & 1;
		f = mant << 7 | exp << 23 | sign << 31;
	}
	else
		f = (v & 0x800000) << 8;
	float out;
	memcpy (&out, &f, 4);
	return out;
}

// ---------------------------------------------------------------------------
// structural probes
// ---------------------------------------------------------------------------

int IsGFModel (const u8 *data, size_t size)
{
	// Probe-lenient: the FILETYPE probe is only 2KB, so hash tables that
	// run past `size` are accepted on the strength of the header. Full
	// validation happens at decode time.
	if (!data || size < 0x20 || rd_le32 (data) != GF_MAGIC_MODEL)
		return 0;
	gfr_t r = { data, size, 4, 0 };
	u32 nsect = gfr_u32 (&r);
	if (r.err || !nsect || nsect > 256)
		return 0;
	gfr_align16 (&r);
	size_t pay = 0;
	if (!gfr_section (&r, "gfmodel", &pay) || r.err)
		return 0;
	// first hash table (shaders) must parse: count sane, names NUL-padded
	if (r.p + 4 > r.n)
		return 1; // truncated probe: header matched
	u32 n = rd_le32 (data + r.p);
	if (n > 4096)
		return 0;
	r.p += 4;
	for (u32 i = 0; i < n; i++)
	{
		if (r.p + 4 + 0x40 > r.n)
			return 1; // truncated probe: accept on header
		r.p += 4 + 0x40;
	}
	return 1;
}

int IsGFTexture (const u8 *data, size_t size)
{
	if (!data || size < 0x90 || rd_le32 (data) != GF_MAGIC_TEXTURE)
		return 0;
	if (rd_le32 (data + 4) != 1)
		return 0;
	if (strncmp ((const char *)data + 8, "texture", 7))
		return 0;
	u32 rawlen = rd_le32 (data + 0x18);
	u16 w = data[0x68] | (u16)data[0x69] << 8;
	u16 h = data[0x6a] | (u16)data[0x6b] << 8;
	u16 fmt = data[0x6c] | (u16)data[0x6d] << 8;
	if (!w || !h || w > 4096 || h > 4096)
		return 0;
	// GFTextureFormat codes known from SPICA GFTextureFormat.cs
	switch (fmt)
	{
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x16:
		case 0x17:
		case 0x23:
		case 0x24:
		case 0x25:
		case 0x26:
		case 0x27:
		case 0x28:
		case 0x29:
		case 0x2a:
		case 0x2b:
			break;
		default:
			return 0;
	}
	if ((u64)0x80 + rawlen > size)
		return 0;
	return 1;
}

int IsGFMotion (const u8 *data, size_t size)
{
	if (!data || size < 0x30 || rd_le32 (data) != GF_MAGIC_MOTION)
		return 0;
	u32 nsect = rd_le32 (data + 4);
	if (!nsect || nsect > 8)
		return 0;
	if (8 + (size_t)nsect * 12 > size)
		return 0;
	// section 0 must be the SubHeader (id 0); other ids are 1/3/6
	if (rd_le32 (data + 8) != 0)
		return 0;
	for (u32 i = 0; i < nsect; i++)
	{
		u32 id = rd_le32 (data + 8 + i * 12);
		u32 len = rd_le32 (data + 12 + i * 12);
		u32 addr = rd_le32 (data + 16 + i * 12);
		if (id != 0 && id != 1 && id != 3 && id != 6)
			return 0;
		if ((u64)addr + len > size)
			return 0;
	}
	return 1;
}

int IsGFModelPack (const u8 *data, size_t size)
{
	if (!data || size < 0x30 || rd_le32 (data) != GF_MAGIC_MODELPACK)
		return 0;
	u32 total = 0;
	for (int s = 0; s < 5; s++)
	{
		u32 c = rd_le32 (data + 4 + s * 4);
		if (c > 4096)
			return 0;
		total += c;
	}
	if (!total || total > 4096)
		return 0;
	if (0x18 + (size_t)total * 4 > size)
		return 0;
	// every pointer must land inside the file and on a readable entry
	size_t tab = 0x18;
	for (u32 i = 0; i < total; i++)
	{
		u32 ptr = rd_le32 (data + tab + i * 4);
		if ((size_t)ptr + 6 > size)
			return 0;
	}
	return 1;
}

// Gen6/Gen7 package (SPICA.WinForms GFPackage): 2 uppercase ASCII bytes,
// u16 count, then count+1 u32 offsets at 0x04. Genuine files are >= 0x80
// bytes and every offset lands in bounds in non-decreasing order.
int IsGFPackage (const u8 *data, size_t size)
{
	if (!data || size < 0x80)
		return 0;
	if (data[0] < 'A' || data[0] > 'Z' || data[1] < 'A' || data[1] > 'Z')
		return 0;
	u32 count = data[2] | (u32)data[3] << 8;
	if (!count || count > 100000)
		return 0;
	if (4 + ((size_t)count + 1) * 4 > size)
		return 0;
	u32 prev = 0;
	for (u32 i = 0; i <= count; i++)
	{
		u32 off = rd_le32 (data + 4 + i * 4);
		if (off > size || off < prev)
			return 0;
		prev = off;
	}
	// the first member must start at or after the offset table
	if (rd_le32 (data + 4) < 4 + (count + 1) * 4)
		return 0;
	return 1;
}

int IsGFLXPack (const u8 *data, size_t size)
{
	if (!data || size < 0x38 || memcmp (data, "GFLXPACK", 8))
		return 0;
	u32 count = rd_le32 (data + 0x10);
	u64 info = (u64)rd_le32 (data + 0x18) | (u64)rd_le32 (data + 0x1c) << 32;
	if (!count || count > 100000)
		return 0;
	if (info >= size || info + (u64)count * 24 > size)
		return 0;
	// The plain 3DS member table shares magic, count and info offsets, so
	// tell them apart through entry 0: a GFLXPack entry is (id, decomp,
	// comp, dummy, dataOff) with LZ4-plausible sizes, while a plain entry
	// carries a u16 zip code (1..3) at offset +2 (see ExtractGFPAKArchive).
	// This is a heuristic, but the xx chain tries GFLXPack first with
	// strict per-entry LZ4 validation and falls through to the plain
	// extractor, so a misprobe never loses data.
	u32 id = rd_le32 (data + info);
	u32 dlen = rd_le32 (data + info + 4);
	u32 clen = rd_le32 (data + info + 8);
	u64 doff = (u64)rd_le32 (data + info + 16) | (u64)rd_le32 (data + info + 20) << 32;
	u16 zip = data[info + 2] | (u16)data[info + 3] << 8;
	(void)id;
	if (!dlen || dlen > 0x4000000 || !clen || clen > 0x4000000 || clen > dlen)
		return 0;
	if (doff >= size || doff + clen > size)
		return 0;
	if (zip >= 1 && zip <= 3)
		return 0; // plain member table
	return 1;
}

// XY/ORAS motion pack (SPICA GF1MotionPack): u32 count, then count u32
// offsets; entry 0 is the skeleton, the rest are animations (0 = absent).
int IsGF1Motion (const u8 *data, size_t size)
{
	if (!data || size < 0x20)
		return 0;
	u32 count = rd_le32 (data);
	if (count < 2 || count > 4096)
		return 0;
	if (4 + (size_t)count * 4 > size)
		return 0;
	u32 skel = rd_le32 (data + 4);
	if (!skel || skel >= size)
		return 0;
	// skeleton starts with u8 bone count + u8 first-bone index
	if (data[skel] < 1 || data[skel] > 200)
		return 0;
	for (u32 i = 1; i < count; i++)
	{
		u32 off = rd_le32 (data + 4 + i * 4);
		if (off >= size)
			return 0;
	}
	return 1;
}

// ---------------------------------------------------------------------------
// GFTexture -> RGBA8
// ---------------------------------------------------------------------------

static uint gf_tex_to_pica (uint fmt)
{
	// SPICA GFTextureFormat -> PICATextureFormat (H3DTexture.ToH3DTexture).
	switch (fmt)
	{
		case 0x02:
			return 3; // RGB565
		case 0x03:
			return 1; // RGB8
		case 0x04:
			return 0; // RGBA8
		case 0x16:
			return 4; // RGBA4
		case 0x17:
			return 2; // RGBA5551
		case 0x23:
			return 5; // LA8
		case 0x24:
			return 6; // HiLo8
		case 0x25:
			return 7; // L8
		case 0x26:
			return 8; // A8
		case 0x27:
			return 9; // LA4
		case 0x28:
			return 10; // L4
		case 0x29:
			return 11; // A4
		case 0x2a:
			return 12; // ETC1
		case 0x2b:
			return 13; // ETC1A4
		default:
			return 0xffffffffu;
	}
}

void GetGFTextureName (char *buf, size_t bufsz, const u8 *data, size_t size)
{
	if (buf && bufsz)
		buf[0] = 0;
	if (!buf || !bufsz || !IsGFTexture (data, size))
		return;
	size_t copy = 0x40 < bufsz - 1 ? 0x40 : bufsz - 1;
	memcpy (buf, data + 0x28, copy);
	buf[copy] = 0;
	// trim at first NUL (name field is NUL-padded, not terminated)
	buf[strnlen (buf, copy)] = 0;
}

enumError DecodeGFTexture_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, size_t size)
{
	if (!dest || !width || !height)
		return EINVAL;
	if (!IsGFTexture (data, size))
		return EINVAL;
	u16 w = data[0x68] | (u16)data[0x69] << 8;
	u16 h = data[0x6a] | (u16)data[0x6b] << 8;
	u16 fmt = data[0x6c] | (u16)data[0x6d] << 8;
	u32 rawlen = rd_le32 (data + 0x18);
	uint pica = gf_tex_to_pica (fmt);
	if (pica > 13)
		return EINVAL;
	return DecodePicaTexture (dest, width, height, data + 0x80, w, h, pica, (uint)(size - 0x80) < rawlen ? (uint)(size - 0x80) : rawlen);
}

// ---------------------------------------------------------------------------
// PICA200 command-buffer walk (SPICA PICACommandReader semantics)
// ---------------------------------------------------------------------------

typedef struct
{
	const u32 *w;
	u32 n; // word count
} pica_cmds_t;

// Calls cb(reg, params, n_params, ctx) for every command. Consecutive-write
// commands emit one call per register (reg + k), like SPICA's GetCommand.
static void pica_walk (const pica_cmds_t *c, void (*cb) (u32 reg, const u32 *par, void *ctx), void *ctx)
{
	if (!c || !c->w || !cb)
		return;
	u32 i = 0;
	while (i + 1 < c->n)
	{
		u32 param = c->w[i++];
		u32 cmd = c->w[i++];
		u32 id = cmd & 0xffff;
		u32 mask = (cmd >> 16) & 0xf;
		u32 extra = (cmd >> 20) & 0x7ff;
		int consec = (cmd >> 31) & 1;
		(void)mask;
		if (consec)
		{
			u32 p = param;
			for (u32 k = 0; k <= extra; k++)
			{
				cb (id + k, &p, ctx);
				if (k < extra && i < c->n)
					p = c->w[i++];
			}
		}
		else
		{
			cb (id, &param, ctx);
			for (u32 k = 0; k < extra && i < c->n; k++)
				i++;
			if ((extra + 1) & 1 && extra > 0 && i < c->n)
				i++;
		}
	}
}

// --- enable-list state (SPICA GFMesh enable-command parsing) ---

typedef struct
{
	u64 formats;
	u64 attrs;
	u64 perm;
	u32 fixed_idx;
	u32 fixed[12][3];
	int n_attr; // VSH_NUM_ATTR + 1
	int stride;
} gf_en_t;

static void gf_en_cb (u32 reg, const u32 *par, void *ctx)
{
	gf_en_t *e = ctx;
	u32 p = *par;
	switch (reg)
	{
		case 0x201:
			e->formats |= (u64)p;
			break;
		case 0x202:
			e->formats |= (u64)p << 32;
			break;
		case 0x204:
			e->attrs |= p;
			break;
		case 0x205:
			e->attrs |= (u64)(p & 0xffff) << 32;
			e->stride = (p >> 16) & 0xff;
			break;
		case 0x232:
			e->fixed_idx = p < 12 ? p : 0;
			break;
		case 0x233:
			e->fixed[e->fixed_idx][0] = p;
			break;
		case 0x234:
			e->fixed[e->fixed_idx][1] = p;
			break;
		case 0x235:
			e->fixed[e->fixed_idx][2] = p;
			break;
		case 0x242:
			e->n_attr = (int)p + 1;
			break;
		case 0x2bb:
			e->perm |= (u64)p;
			break;
		case 0x2bc:
			e->perm |= (u64)p << 32;
			break;
		default:
			break;
	}
}

// --- index-list state ---

typedef struct
{
	u32 addr;
	u32 count;
	u32 prim; // (PRIMITIVE_CONFIG >> 8): 0 tris, 1 strip, 2 fan
} gf_idx_t;

static void gf_idx_cb (u32 reg, const u32 *par, void *ctx)
{
	gf_idx_t *s = ctx;
	if (reg == 0x227)
		s->addr = *par;
	else if (reg == 0x228)
		s->count = *par;
	else if (reg == 0x25e)
		s->prim = *par >> 8;
}

// ---------------------------------------------------------------------------
// GFModel -> model_t
// ---------------------------------------------------------------------------

// PICA attribute names (SPICA PICAAttributeName) and formats.
enum
{
	GF_A_POS = 0,
	GF_A_NRM = 1,
	GF_A_TAN = 2,
	GF_A_COL = 3,
	GF_A_UV0 = 4,
	GF_A_UV1 = 5,
	GF_A_UV2 = 6,
	GF_A_BONE = 7,
	GF_A_WEIGHT = 8
};

typedef struct
{
	uint name;
	uint fmt; // 0 s8, 1 u8, 2 s16, 3 f32
	uint elems;
	float scale;
	int fixed; // nonzero: constant for the whole submesh
	float fval[4];
} gf_attr_t;

static float gf_scales[4] = { 1.0f / 127, 1.0f / 255, 1.0f / 32767, 1.0f };

static float gf_read_elem (const u8 *p, uint fmt, uint el)
{
	switch (fmt)
	{
		case 0:
			return (float)(s8)p[el];
		case 1:
			return (float)p[el];
		case 2:
		{
			u16 v = (u16)p[el * 2] | (u16)p[el * 2 + 1] << 8;
			return (float)(s16)v;
		}
		default:
		{
			u32 u = rd_le32 (p + el * 4);
			float f;
			memcpy (&f, &u, 4);
			return f;
		}
	}
}

static uint gf_elem_size (uint fmt, uint elems)
{
	return elems * (fmt == 3 ? 4 : fmt == 2 ? 2 : 1);
}

// skinning palette: dedup (bones[4], weights[4]) combos (lib-bcres.c idiom)
typedef struct
{
	int bones[4];
	float weights[4];
	int n;
} gf_inf_t;

static int gf_inf_add (gf_inf_t **tab, size_t *n, size_t *cap, const int *bones, const float *w, int cnt)
{
	for (size_t i = 0; i < *n; i++)
	{
		if ((*tab)[i].n != cnt)
			continue;
		int same = 1;
		for (int k = 0; k < cnt; k++)
			if ((*tab)[i].bones[k] != bones[k]
				|| fabsf ((*tab)[i].weights[k] - w[k]) > 1e-6f)
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
		gf_inf_t *nt = REALLOC (*tab, ncap * sizeof (**tab));
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

// material scan: name + up to 3 texture names + diffuse colour
typedef struct
{
	char name[64];
	char tex[3][64];
	float diffuse[4];
} gf_mat_t;

static int gf_parse_material (const u8 *data, size_t size, size_t off, gf_mat_t *out)
{
	gfr_t r = { data, size, off, 0 };
	size_t pay = 0;
	if (!gfr_section (&r, "material", &pay) || r.err)
		return 0;
	// 4 hash-names: material, shader, vtx-shader, frag-shader
	for (int i = 0; i < 4; i++)
	{
		gfr_u32 (&r); // hash
		char nm[256];
		gfr_blen_str (&r, nm, sizeof (nm));
		if (r.err)
			return 0;
		if (i == 0 && out)
			snprintf (out->name, sizeof (out->name), "%s", nm);
	}
	gfr_skip (&r, 12 + 4 + 8); // LUT hashes, padding, bump+assigns
	if (r.err)
		return 0;
	float cols[12][4];
	for (int i = 0; i < 12; i++)
		for (int k = 0; k < 4; k++)
			cols[i][k] = (float)gfr_u8 (&r) / 255.0f;
	if (r.err)
		return 0;
	if (out)
		memcpy (out->diffuse, cols[11], sizeof (cols[11]));
	gfr_skip (&r, (3 + 1 + 4 + 1 + 1 + 9 + 1) * 4 + 4 * 4); // edge/proj/rim/flags/bake/vtxtype/params
	u32 nunits = gfr_u32 (&r);
	if (r.err || nunits > 8)
		return 0;
	for (u32 u = 0; u < nunits; u++)
	{
		gfr_u32 (&r); // hash
		char nm[256];
		gfr_blen_str (&r, nm, sizeof (nm));
		// unit, mapping, scale(2), rot, trans(2), wrapU/V, mag, min, minLOD
		gfr_skip (&r, 1 + 1 + 8 + 4 + 8 + 20);
		if (r.err)
			return 0;
		if (out && u < 3 && nm[0])
			snprintf (out->tex[u], sizeof (out->tex[u]), "%s", nm);
	}
	return 1;
}

void *ParseGFModel (const u8 *data, size_t size)
{
	if (!IsGFModel (data, size))
		return NULL;
	gfr_t r = { data, size, 8, 0 }; // magic + section count already validated
	gfr_align16 (&r);
	size_t mpay = 0;
	size_t mlen = gfr_section (&r, "gfmodel", &mpay);
	if (r.err)
		return NULL;
	size_t mend = mpay + mlen;

	// 4 hash tables: shaders, textures, materials, meshes
	u32 n_tex = 0, n_mat = 0, n_mesh = 0;
	char (*mat_names)[0x40] = NULL;
	char (*mesh_names)[0x40] = NULL;
	for (int t = 0; t < 4; t++)
	{
		u32 n = gfr_u32 (&r);
		if (r.err || n > 4096)
			goto fail;
		for (u32 i = 0; i < n; i++)
		{
			gfr_skip (&r, 4);
			if (t == 2 && i < 256)
			{
				// material names
			}
			char nm[0x40];
			if (r.p + 0x40 > r.n)
			{
				r.err = 1;
				break;
			}
			memcpy (nm, r.d + r.p, 0x40);
			nm[0x3f] = 0;
			gfr_skip (&r, 0x40);
			if (r.err)
				break;
			if (t == 2)
			{
				char (*nn)[0x40] = REALLOC (mat_names, (n_mat + 1) * 0x40);
				if (!nn)
				{
					r.err = 1;
					break;
				}
				mat_names = nn;
				memcpy (mat_names[n_mat++], nm, 0x40);
			}
			else if (t == 3)
			{
				char (*nn)[0x40] = REALLOC (mesh_names, (n_mesh + 1) * 0x40);
				if (!nn)
				{
					r.err = 1;
					break;
				}
				mesh_names = nn;
				memcpy (mesh_names[n_mesh++], nm, 0x40);
			}
			else if (t == 1)
				n_tex++;
		}
		if (r.err)
			goto fail;
	}
	(void)n_tex;

	gfr_skip (&r, 32 + 64); // bbox min/max + transform
	u32 unk_len = gfr_u32 (&r);
	u32 unk_off = gfr_u32 (&r);
	gfr_skip (&r, 8);
	gfr_skip (&r, unk_off + unk_len);
	s32 nbones = (s32)gfr_u32 (&r);
	gfr_skip (&r, 12);
	if (r.err || nbones < 0 || nbones > 4096)
		goto fail;

	model_t *out = CALLOC (1, sizeof (model_t));
	if (!out)
		goto fail;

	// skeleton
	if (nbones > 0)
	{
		out->joints = CALLOC ((size_t)nbones, sizeof (joint_t));
		if (!out->joints)
		{
			FREE (out);
			goto fail;
		}
		char (*bone_names)[128] = CALLOC ((size_t)nbones, 128);
		char (*bone_par)[128] = CALLOC ((size_t)nbones, 128);
		if (!bone_names || !bone_par)
		{
			FREE (bone_names);
			FREE (bone_par);
			FreeModel (out);
			goto fail;
		}
		for (s32 b = 0; b < nbones; b++)
		{
			gfr_blen_str (&r, bone_names[b], 128);
			gfr_blen_str (&r, bone_par[b], 128);
			gfr_skip (&r, 1); // flags
			float sx = gfr_f32 (&r), sy = gfr_f32 (&r), sz = gfr_f32 (&r);
			float rx = gfr_f32 (&r), ry = gfr_f32 (&r), rz = gfr_f32 (&r);
			float tx = gfr_f32 (&r), ty = gfr_f32 (&r), tz = gfr_f32 (&r);
			if (r.err)
			{
				FREE (bone_names);
				FREE (bone_par);
				FreeModel (out);
				goto fail;
			}
			snprintf (out->joints[b].name, sizeof (out->joints[b].name), "%s", bone_names[b]);
			// SPICA stores euler radians; model_t rotate is degrees
			out->joints[b].scale.x = sx;
			out->joints[b].scale.y = sy;
			out->joints[b].scale.z = sz;
			out->joints[b].rotate.x = rx * (float)(180.0 / M_PI);
			out->joints[b].rotate.y = ry * (float)(180.0 / M_PI);
			out->joints[b].rotate.z = rz * (float)(180.0 / M_PI);
			out->joints[b].translate.x = tx;
			out->joints[b].translate.y = ty;
			out->joints[b].translate.z = tz;
			out->joints[b].parent_idx = -1;
		}
		for (s32 b = 0; b < nbones; b++)
		{
			if (!bone_par[b][0])
				continue;
			for (s32 p = 0; p < nbones; p++)
				if (!strcmp (bone_names[p], bone_par[b]))
				{
					out->joints[b].parent_idx = p;
					break;
				}
		}
		FREE (bone_names);
		FREE (bone_par);
		out->num_joints = (size_t)nbones;
		ComputeModelTRSBinds (out);
	}

	gfr_align16 (&r);
	gfr_u32 (&r); // LUT count
	gfr_u32 (&r); // LUT length
	if (r.err)
	{
		FreeModel (out);
		goto fail;
	}
	gfr_align16 (&r);
	// NOTE: LUT blobs are skipped implicitly: materials/meshes are located
	// by walking forward, and every section carries its own length.
	// Materials are n_mat entries back to back.
	gf_mat_t *mats = NULL;
	if (n_mat)
	{
		mats = CALLOC (n_mat, sizeof (*mats));
		if (!mats)
		{
			FreeModel (out);
			goto fail;
		}
		out->materials = CALLOC (n_mat, sizeof (material_t));
		if (!out->materials)
		{
			FREE (mats);
			FreeModel (out);
			goto fail;
		}
		out->num_materials = n_mat;
	}
	// LUTs sit between the header and the materials but carry no count we
	// can trust blindly (retail writes 0x420-length blobs only when LUTs
	// exist); instead scan forward for "material" sections.
	for (u32 m = 0; m < n_mat; m++)
	{
		size_t moff = (size_t)-1;
		// scan at 16-byte granularity for the next material section
		for (size_t p = (r.p + 0xf) & ~(size_t)0xf; p + 16 <= size; p += 16)
		{
			if (!memcmp (data + p, "material", 8))
			{
				moff = p;
				break;
			}
			// stop if we ran into the first mesh section instead
			if (!memcmp (data + p, "mesh", 4) && data[p + 4] == 0)
				break;
		}
		if (moff == (size_t)-1)
			break;
		gf_mat_t *gm = &mats[m];
		if (mat_names && m < n_mat)
			snprintf (gm->name, sizeof (gm->name), "%s", mat_names[m]);
		gm->diffuse[0] = gm->diffuse[1] = gm->diffuse[2] = 0.8f;
		gm->diffuse[3] = 1.0f;
		gf_parse_material (data, size, moff, gm);
		material_t *dm = &out->materials[m];
		snprintf (dm->name, sizeof (dm->name), "%s", gm->name[0] ? gm->name : mat_names[m]);
		memcpy (dm->diffuse, gm->diffuse, sizeof (dm->diffuse));
		dm->num_textures = 0;
		for (int t = 0; t < 3; t++)
			if (gm->tex[t][0])
			{
				snprintf (dm->textures[dm->num_textures], sizeof (dm->textures[0]), "%s", gm->tex[t]);
				dm->texture_coord[dm->num_textures] = t;
				dm->wrap_s[dm->num_textures] = dm->wrap_t[dm->num_textures] = 1;
				dm->min_filter[dm->num_textures] = dm->mag_filter[dm->num_textures] = 1;
				dm->num_textures++;
			}
		// advance past this material
		gfr_t mr = { data, size, moff, 0 };
		size_t pp = 0;
		size_t ml = gfr_section (&mr, "material", &pp);
		r.p = mr.err ? size : pp + ml;
		if (r.p > size)
			r.p = size;
	}

	// meshes: n_mesh "mesh" sections back to back
	gf_inf_t *inf = NULL;
	size_t n_inf = 0, cap_inf = 0;
	for (u32 mi = 0; mi < n_mesh; mi++)
	{
		size_t moff = (size_t)-1;
		for (size_t p = (r.p + 0xf) & ~(size_t)0xf; p + 16 <= size; p += 16)
		{
			if (!memcmp (data + p, "mesh", 4) && data[p + 4] == 0)
			{
				moff = p;
				break;
			}
		}
		if (moff == (size_t)-1)
			break;
		gfr_t q = { data, size, moff, 0 };
		size_t mpos = 0;
		size_t mesh_len = gfr_section (&q, "mesh", &mpos);
		(void)mesh_len;
		if (q.err)
			break;
		gfr_u32 (&q); // name hash
		char mname[0x40];
		if (q.p + 0x40 > q.n)
			break;
		memcpy (mname, q.d + q.p, 0x40);
		mname[0x3f] = 0;
		gfr_skip (&q, 0x40 + 4 + 32);
		u32 nsub = gfr_u32 (&q);
		gfr_skip (&q, 4 + 16); // boneIndicesPerVertex + padding
		if (q.err || !nsub || nsub > 4096)
		{
			r.p = q.p;
			continue;
		}
		// command buffers: nsub*3 entries
		typedef struct
		{
			const u32 *w;
			u32 n;
		} cmd_t;
		cmd_t *cmds = CALLOC (nsub * 3, sizeof (*cmds));
		if (!cmds)
		{
			r.p = q.p;
			continue;
		}
		int cmd_ok = 1;
		for (u32 c = 0; c < nsub * 3; c++)
		{
			u32 blen = gfr_u32 (&q);
			gfr_skip (&q, 12); // index, count, padding
			if (q.err || blen > 0x1000000 || q.p + blen > q.n || (blen & 3))
			{
				cmd_ok = 0;
				break;
			}
			cmds[c].w = (const u32 *)(q.d + q.p);
			cmds[c].n = blen / 4;
			gfr_skip (&q, blen);
		}
		if (!cmd_ok || q.err)
		{
			FREE (cmds);
			r.p = q.p;
			continue;
		}
		// submesh descriptors
		for (u32 s = 0; s < nsub; s++)
		{
			gfr_u32 (&q); // name hash
			u32 nm_len = (u32)(s32)gfr_u32 (&q);
			char sname[256];
			if (q.err || nm_len > 250 || q.p + nm_len > q.n)
			{
				q.err = 1;
				break;
			}
			size_t cp = nm_len < sizeof (sname) - 1 ? nm_len : sizeof (sname) - 1;
			memcpy (sname, q.d + q.p, cp);
			sname[cp] = 0;
			sname[strnlen (sname, cp)] = 0;
			gfr_skip (&q, nm_len);
			u8 bonecnt = gfr_u8 (&q);
			u8 bonemap[31];
			for (int b = 0; b < 31; b++)
				bonemap[b] = gfr_u8 (&q);
			u32 vtx_cnt = gfr_u32 (&q);
			u32 idx_cnt = gfr_u32 (&q);
			u32 vtx_len = gfr_u32 (&q);
			u32 idx_len = gfr_u32 (&q);
			if (q.err || vtx_cnt > 1000000 || idx_cnt > 3000000 || vtx_len > 0x2000000
				|| idx_len > 0x2000000)
			{
				q.err = 1;
				break;
			}

			// decode enable list
			gf_en_t en;
			memset (&en, 0, sizeof (en));
			pica_cmds_t pc = { cmds[s * 3].w, cmds[s * 3].n };
			pica_walk (&pc, gf_en_cb, &en);
			gf_idx_t ix;
			memset (&ix, 0, sizeof (ix));
			pica_cmds_t pi = { cmds[s * 3 + 2].w, cmds[s * 3 + 2].n };
			pica_walk (&pi, gf_idx_cb, &ix);
			if (!en.n_attr || en.n_attr > 16 || !en.stride || en.stride > 128 || !ix.count
				|| ix.count > 3000000)
				continue;

			// build attribute list (SPICA GFMesh enable parsing)
			gf_attr_t attrs[16];
			int n_attrs = 0;
			for (int ia = 0; ia < en.n_attr && n_attrs < 16; ia++)
			{
				if ((en.formats >> (48 + ia)) & 1)
				{
					uint nm2 = (en.perm >> (ia * 4)) & 0xf;
					if (nm2 > 8)
						continue;
					gf_attr_t *a = &attrs[n_attrs++];
					a->name = nm2;
					a->fixed = 1;
					a->fval[0] = gf_float24 (en.fixed[ia][2] & 0xffffff);
					a->fval[1] = gf_float24 ((en.fixed[ia][2] >> 24) | ((en.fixed[ia][1] & 0xffff) << 8));
					a->fval[2] = gf_float24 ((en.fixed[ia][1] >> 16) | ((en.fixed[ia][0] & 0xff) << 16));
					a->fval[3] = gf_float24 (en.fixed[ia][0] >> 8);
					float sc = (nm2 == GF_A_COL || nm2 == GF_A_WEIGHT) ? gf_scales[1] : 1.0f;
					for (int k = 0; k < 4; k++)
						a->fval[k] *= sc;
				}
				else
				{
					int pidx = (en.attrs >> (ia * 4)) & 0xf;
					uint nm2 = (en.perm >> (pidx * 4)) & 0xf;
					uint f = (en.formats >> (pidx * 4)) & 0xf;
					if (nm2 > 8)
						continue;
					gf_attr_t *a = &attrs[n_attrs++];
					a->name = nm2;
					a->fmt = f & 3;
					a->elems = (f >> 2) + 1;
					a->fixed = 0;
					a->scale = nm2 == GF_A_BONE ? 1.0f : gf_scales[f & 3];
				}
			}

			size_t raw_off = q.p;
			if (raw_off + vtx_len > q.n)
				continue;
			// indices follow the vertex blob
			size_t idx_off = raw_off + vtx_len;
			if (idx_off + idx_len > q.n)
				continue;
			// expand index list (8/16-bit per BufferAddress bit 31)
			u32 nicount = ix.count < idx_cnt ? ix.count : idx_cnt;
			u32 *idx = CALLOC (nicount ? nicount : 1, sizeof (*idx));
			if (!idx)
				continue;
			int idx_ok = 1;
			if (ix.addr >> 31)
			{
				if (idx_off + (size_t)nicount * 2 > q.n)
					idx_ok = 0;
				else
					for (u32 k = 0; k < nicount; k++)
						idx[k] = q.d[idx_off + k * 2] | (u32)q.d[idx_off + k * 2 + 1] << 8;
			}
			else
			{
				if (idx_off + nicount > q.n)
					idx_ok = 0;
				else
					for (u32 k = 0; k < nicount; k++)
						idx[k] = q.d[idx_off + k];
			}
			if (!idx_ok)
			{
				FREE (idx);
				continue;
			}
			// destripe into triangles
			u32 *tri = NULL;
			size_t ntri = 0;
			if (ix.prim == 0)
			{
				tri = idx;
				ntri = nicount / 3 * 3;
			}
			else
			{
				tri = CALLOC (nicount ? nicount : 1, sizeof (*tri));
				if (!tri)
				{
					FREE (idx);
					continue;
				}
				for (u32 k = 0; k + 2 < nicount; k++)
				{
					u32 a = idx[k], b = idx[k + 1], c = idx[k + 2];
					if (a == b || b == c || a == c)
						continue;
					if (ix.prim == 1 && (k & 1))
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
				idx = NULL;
				if (!ntri)
				{
					FREE (tri);
					continue;
				}
			}

			int has_n = 0, has_uv = 0, has_tan = 0, has_col = 0, has_uv1 = 0, has_uv2 = 0;
			int bi = -1, bw = -1;
			for (int a = 0; a < n_attrs; a++)
			{
				if (attrs[a].name == GF_A_NRM)
					has_n = 1;
				else if (attrs[a].name == GF_A_UV0)
					has_uv = 1;
				else if (attrs[a].name == GF_A_TAN)
					has_tan = 1;
				else if (attrs[a].name == GF_A_COL)
					has_col = 1;
				else if (attrs[a].name == GF_A_UV1)
					has_uv1 = 1;
				else if (attrs[a].name == GF_A_UV2)
					has_uv2 = 1;
				else if (attrs[a].name == GF_A_BONE && !attrs[a].fixed && bi < 0)
					bi = a;
				else if (attrs[a].name == GF_A_WEIGHT && !attrs[a].fixed && bw < 0)
					bw = a;
			}
			// fixed bone streams (rigid binds)
			float fbone[4] = { 0, 0, 0, 0 }, fweight[4] = { 1, 0, 0, 0 };
			int has_fbone = 0, has_fweight = 0;
			for (int a = 0; a < n_attrs; a++)
			{
				if (!attrs[a].fixed)
					continue;
				if (attrs[a].name == GF_A_BONE)
				{
					has_fbone = 1;
					memcpy (fbone, attrs[a].fval, sizeof (fbone));
				}
				else if (attrs[a].name == GF_A_WEIGHT)
				{
					has_fweight = 1;
					memcpy (fweight, attrs[a].fval, sizeof (fweight));
				}
			}

			mesh_t *mesh = NULL;
			{
				mesh_t *nm = REALLOC (out->meshes, (out->num_meshes + 1) * sizeof (*nm));
				if (!nm)
				{
					FREE (tri);
					FREE (idx);
					continue;
				}
				out->meshes = nm;
				mesh = &out->meshes[out->num_meshes];
				memset (mesh, 0, sizeof (*mesh));
			}
			mesh->positions = CALLOC (ntri, sizeof (*mesh->positions));
			mesh->vertices = CALLOC (ntri, sizeof (*mesh->vertices));
			mesh->position_node = CALLOC (ntri, sizeof (*mesh->position_node));
			if (has_n)
				mesh->normals = CALLOC (ntri, sizeof (*mesh->normals));
			if (has_uv)
				mesh->texcoords = CALLOC (ntri, sizeof (*mesh->texcoords));
			if (has_tan)
				mesh->tangents = CALLOC (ntri, sizeof (*mesh->tangents));
			if (has_col)
				mesh->colors[0] = CALLOC (ntri, sizeof (*mesh->colors[0]));
			if (has_uv1)
				mesh->extra_texcoords[0] = CALLOC (ntri, sizeof (*mesh->extra_texcoords[0]));
			if (has_uv2)
				mesh->extra_texcoords[1] = CALLOC (ntri, sizeof (*mesh->extra_texcoords[1]));
			if (!mesh->positions || !mesh->vertices || !mesh->position_node
				|| (has_n && !mesh->normals) || (has_uv && !mesh->texcoords))
			{
				FREE (mesh->positions);
				FREE (mesh->normals);
				FREE (mesh->texcoords);
				FREE (mesh->tangents);
				FREE (mesh->colors[0]);
				FREE (mesh->extra_texcoords[0]);
				FREE (mesh->extra_texcoords[1]);
				FREE (mesh->vertices);
				FREE (mesh->position_node);
				memset (mesh, 0, sizeof (*mesh));
				FREE (tri);
				FREE (idx);
				continue;
			}
			for (size_t k = 0; k < ntri; k++)
				mesh->position_node[k] = -1;

			if (nsub > 1)
				snprintf (mesh->name, sizeof (mesh->name), "%s_%s", mname, sname);
			else
				snprintf (mesh->name, sizeof (mesh->name), "%s", mname);
			// submesh name doubles as the material name (SPICA ToH3DModel)
			mesh->material_idx = -1;
			for (u32 m2 = 0; m2 < n_mat; m2++)
				if (mat_names && !strcmp (mat_names[m2], sname))
				{
					mesh->material_idx = (int)m2;
					break;
				}

			size_t n = 0;
			for (size_t k = 0; k < ntri; k++)
			{
				u32 vi = tri[k];
				if ((size_t)vi * en.stride + en.stride > vtx_len)
					continue;
				const u8 *vo = data + raw_off + (size_t)vi * en.stride;
				size_t vp = 0;
				float pos[4] = { 0, 0, 0, 1 }, nrm[4] = { 0, 0, 0, 0 };
				float tan[4] = { 0, 0, 0, 0 }, col[4] = { 1, 1, 1, 1 };
				float uv[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
				int loci[4] = { 0, 0, 0, 0 };
				float wgt[4] = { 0, 0, 0, 0 };
				int loci_n = 0, wgt_n = 0;
				for (int a = 0; a < n_attrs; a++)
				{
					gf_attr_t *at = &attrs[a];
					if (at->fixed)
					{
						if (at->name == GF_A_POS)
							memcpy (pos, at->fval, sizeof (pos));
						else if (at->name == GF_A_NRM)
							memcpy (nrm, at->fval, sizeof (nrm));
						else if (at->name == GF_A_UV0)
							memcpy (uv[0], at->fval, 3 * sizeof (float));
						continue;
					}
					if (vp + gf_elem_size (at->fmt, at->elems) > (uint)en.stride)
						break;
					// SPICA VerticesConverter aligns Short/Float to 2 bytes
					if ((at->fmt == 2 || at->fmt == 3) && (vp & 1))
						vp++;
					float v[4] = { 0, 0, 0, 0 };
					for (uint e = 0; e < at->elems && e < 4; e++)
						v[e] = gf_read_elem (vo + vp, at->fmt, e) * at->scale;
					vp += gf_elem_size (at->fmt, at->elems);
					switch (at->name)
					{
						case GF_A_POS:
							memcpy (pos, v, sizeof (pos));
							break;
						case GF_A_NRM:
							memcpy (nrm, v, sizeof (nrm));
							break;
						case GF_A_TAN:
							memcpy (tan, v, sizeof (tan));
							break;
						case GF_A_COL:
							memcpy (col, v, sizeof (col));
							break;
						case GF_A_UV0:
							memcpy (uv[0], v, 3 * sizeof (float));
							break;
						case GF_A_UV1:
							memcpy (uv[1], v, 3 * sizeof (float));
							break;
						case GF_A_UV2:
							memcpy (uv[2], v, 3 * sizeof (float));
							break;
						case GF_A_BONE:
							for (uint e = 0; e < at->elems && loci_n < 4; e++)
								loci[loci_n++] = (int)v[e];
							break;
						case GF_A_WEIGHT:
							for (uint e = 0; e < at->elems && wgt_n < 4; e++)
								wgt[wgt_n++] = v[e];
							break;
						default:
							break;
					}
				}
				mesh->positions[n].x = pos[0];
				mesh->positions[n].y = pos[1];
				mesh->positions[n].z = pos[2];
				if (has_n)
				{
					mesh->normals[n].x = nrm[0];
					mesh->normals[n].y = nrm[1];
					mesh->normals[n].z = nrm[2];
				}
				if (has_tan)
				{
					mesh->tangents[n].x = tan[0];
					mesh->tangents[n].y = tan[1];
					mesh->tangents[n].z = tan[2];
				}
				if (has_col)
				{
					mesh->colors[0][n].r = col[0];
					mesh->colors[0][n].g = col[1];
					mesh->colors[0][n].b = col[2];
					mesh->colors[0][n].a = col[3];
				}
				if (has_uv)
				{
					mesh->texcoords[n].u = uv[0][0];
					mesh->texcoords[n].v = uv[0][1];
				}
				if (has_uv1)
				{
					mesh->extra_texcoords[0][n].u = uv[1][0];
					mesh->extra_texcoords[0][n].v = uv[1][1];
				}
				if (has_uv2)
				{
					mesh->extra_texcoords[1][n].u = uv[2][0];
					mesh->extra_texcoords[1][n].v = uv[2][1];
				}
				// skinning: local palette indices -> global joints
				int cb[4] = { -1, -1, -1, -1 };
				float cw[4] = { 0, 0, 0, 0 };
				int cn = 0;
				if (bi >= 0)
				{
					for (int j = 0; j < loci_n && cn < 4; j++)
					{
						int local = loci[j];
						int global = -1;
						if (local >= 0 && local < bonecnt)
							global = bonemap[local];
						else if (bonecnt)
							global = bonemap[0];
						if (global < 0 || global >= nbones)
							continue;
						float w2 = j < wgt_n ? wgt[j] : (j == 0 ? 1.0f : 0.0f);
						if (w2 <= 0.0f)
							continue;
						cb[cn] = global;
						cw[cn] = w2;
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
				}
				else if (has_fbone || has_fweight)
				{
					int li = (int)fbone[0];
					int global = (li >= 0 && li < bonecnt) ? bonemap[li] : -1;
					if (global >= 0 && global < nbones)
					{
						cb[0] = global;
						cw[0] = has_fweight ? fweight[0] : 1.0f;
						cn = cw[0] > 0 ? 1 : 0;
					}
				}
				else if (bonecnt && bonemap[0] < nbones)
				{
					cb[0] = bonemap[0];
					cw[0] = 1.0f;
					cn = 1;
				}
				if (cn > 0)
				{
					int ii = gf_inf_add (&inf, &n_inf, &cap_inf, cb, cw, cn);
					if (ii >= 0)
						mesh->position_node[n] = ii;
				}
				mesh->vertices[n].position_idx = (int)n;
				mesh->vertices[n].normal_idx = has_n ? (int)n : -1;
				mesh->vertices[n].texcoord_idx = has_uv ? (int)n : -1;
				mesh->vertices[n].tangent_idx = has_tan ? (int)n : -1;
				mesh->vertices[n].color_idx[0] = has_col ? (int)n : -1;
				mesh->vertices[n].color_idx[1] = -1;
				mesh->vertices[n].extra_texcoord_idx[0] = has_uv1 ? (int)n : -1;
				mesh->vertices[n].extra_texcoord_idx[1] = has_uv2 ? (int)n : -1;
				mesh->vertices[n].extra_texcoord_idx[2] = -1;
				mesh->vertices[n].extra_texcoord_idx[3] = -1;
				mesh->vertices[n].extra_texcoord_idx[4] = -1;
				mesh->vertices[n].extra_texcoord_idx[5] = -1;
				mesh->vertices[n].extra_texcoord_idx[6] = -1;
				n++;
			}
			FREE (tri);
			FREE (idx);
			if (!n)
			{
				FREE (mesh->positions);
				FREE (mesh->normals);
				FREE (mesh->texcoords);
				FREE (mesh->tangents);
				FREE (mesh->colors[0]);
				FREE (mesh->extra_texcoords[0]);
				FREE (mesh->extra_texcoords[1]);
				FREE (mesh->vertices);
				FREE (mesh->position_node);
				memset (mesh, 0, sizeof (*mesh));
				continue;
			}
			mesh->num_positions = mesh->num_vertices = n;
			mesh->num_normals = has_n ? n : 0;
			mesh->num_texcoords = has_uv ? n : 0;
			mesh->num_tangents = has_tan ? n : 0;
			mesh->num_colors[0] = has_col ? n : 0;
			mesh->num_extra_texcoords[0] = has_uv1 ? n : 0;
			mesh->num_extra_texcoords[1] = has_uv2 ? n : 0;
			if (!has_n)
			{
				FREE (mesh->normals);
				mesh->normals = NULL;
			}
			if (!has_uv)
			{
				FREE (mesh->texcoords);
				mesh->texcoords = NULL;
			}
			if (!has_tan)
			{
				FREE (mesh->tangents);
				mesh->tangents = NULL;
			}
			if (!has_col)
			{
				FREE (mesh->colors[0]);
				mesh->colors[0] = NULL;
			}
			if (!has_uv1)
			{
				FREE (mesh->extra_texcoords[0]);
				mesh->extra_texcoords[0] = NULL;
			}
			if (!has_uv2)
			{
				FREE (mesh->extra_texcoords[1]);
				mesh->extra_texcoords[1] = NULL;
			}
			out->num_meshes++;
		}
		FREE (cmds);
		if (q.err)
		{
			r.p = q.p;
			break;
		}
		r.p = q.p;
		(void)mend;
	}

	FREE (mat_names);
	FREE (mesh_names);
	FREE (mats);
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
		// remap position_node: gf_inf_add indices already match order
	}
	FREE (inf);
	if (!out->num_meshes)
	{
		FreeModel (out);
		return NULL;
	}
	return out;

fail:
	FREE (mat_names);
	FREE (mesh_names);
	return NULL;
}

// ---------------------------------------------------------------------------
// motion manifests
// ---------------------------------------------------------------------------

static const char *gf_sect_name (u32 id)
{
	switch (id)
	{
		case 0:
			return "SubHeader";
		case 1:
			return "Skeletal";
		case 3:
			return "Material";
		case 6:
			return "Visibility";
		default:
			return "?";
	}
}

enumError DecodeGFMotion_Text (FILE *f, const u8 *data, size_t size)
{
	if (!f)
		return EINVAL;
	if (!IsGFMotion (data, size))
		return EINVAL;
	u32 nsect = rd_le32 (data + 4);
	fprintf (f, "# GFMotion v1 (Game Freak 3DS skeletal/material/visibility animation)\n");
	fprintf (f, "sections: %u\n", nsect);
	for (u32 i = 0; i < nsect; i++)
		fprintf (f, "section[%u]: id=%u (%s) length=%u address=0x%x\n", i, rd_le32 (data + 8 + i * 12),
			gf_sect_name (rd_le32 (data + 8 + i * 12)), rd_le32 (data + 12 + i * 12),
			rd_le32 (data + 16 + i * 12));
	// subheader
	u32 saddr = rd_le32 (data + 16);
	gfr_t r = { data, size, saddr, 0 };
	u32 frames = gfr_u32 (&r);
	u16 loop = gfr_u16 (&r);
	gfr_u16 (&r);
	float rmin[3], rmax[3];
	for (int k = 0; k < 3; k++)
		rmin[k] = gfr_f32 (&r);
	for (int k = 0; k < 3; k++)
		rmax[k] = gfr_f32 (&r);
	u32 hash = gfr_u32 (&r);
	if (r.err)
		return EINVAL;
	fprintf (f, "frames: %u\nloop: %s\n", frames, (loop & 1) ? "yes" : "no");
	fprintf (f, "region_min: %g %g %g\nregion_max: %g %g %g\nanim_hash: 0x%08x\n", rmin[0], rmin[1],
		rmin[2], rmax[0], rmax[1], rmax[2], hash);
	// content sections
	for (u32 i = 1; i < nsect; i++)
	{
		u32 id = rd_le32 (data + 8 + i * 12);
		u32 addr = rd_le32 (data + 16 + i * 12);
		gfr_t q = { data, size, addr, 0 };
		if (id == 1)
		{
			s32 nb = (s32)gfr_u32 (&q);
			u32 nl = gfr_u32 (&q);
			if (q.err || nb < 0 || nb > 4096 || nl > size)
			{
				fprintf (f, "skeletal: <truncated>\n");
				continue;
			}
			fprintf (f, "skeletal_bones: %d\n", nb);
			size_t bstart = q.p;
			for (s32 b = 0; b < nb; b++)
			{
				char nm[256];
				gfr_blen_str (&q, nm, sizeof (nm));
				if (q.err)
					break;
				fprintf (f, "bone[%d]: %s\n", b, nm);
			}
			if (q.err || q.p != bstart + nl)
			{
				fprintf (f, "skeletal: <truncated names>\n");
				continue;
			}
			static const char *tracks[9]
				= { "SX", "SY", "SZ", "RX", "RY", "RZ", "TX", "TY", "TZ" };
			for (s32 b = 0; b < nb; b++)
			{
				u32 flags = gfr_u32 (&q);
				u32 len = gfr_u32 (&q);
				if (q.err)
					break;
				size_t tend = q.p + len;
				fprintf (f, "bone_anim[%d]: axis_angle=%s", b, (flags >> 31) ? "no" : "yes");
				u32 fl = flags;
				for (int t = 0; t < 9; t++)
				{
					fprintf (f, " %s=%u", tracks[t], fl & 7);
					fl >>= 3;
				}
				fprintf (f, "\n");
				// walk tracks to validate + report constants
				fl = flags;
				for (int t = 0; t < 9 && !q.err; t++)
				{
					u32 code = fl & 7;
					fl >>= 3;
					if (code == 3)
					{
						float v = gfr_f32 (&q);
						if (!q.err)
							fprintf (f, "  const %s = %g\n", tracks[t], v);
					}
					else if (code == 4 || code == 5)
					{
						u32 nk = gfr_u32 (&q);
						if (q.err || nk > 100000)
						{
							q.err = 1;
							break;
						}
						int wide = frames > 0xff;
						for (u32 k = 0; k < nk; k++)
						{
							if (wide)
								gfr_u16 (&q);
							else
								gfr_u8 (&q);
						}
						while ((q.p & 3) && !q.err)
							gfr_u8 (&q);
						if (code == 5)
							gfr_skip (&q, (size_t)nk * 8);
						else
							gfr_skip (&q, 16 + (size_t)nk * 4);
						if (!q.err)
							fprintf (f, "  track %s: %u keys\n", tracks[t], nk);
					}
				}
				if (!q.err && q.p != tend)
					q.p = tend > q.n ? q.n : tend;
				if (q.err)
				{
					fprintf (f, "skeletal: <truncated tracks>\n");
					break;
				}
			}
		}
		else if (id == 3)
		{
			s32 nm2 = (s32)gfr_u32 (&q);
			u32 nl = gfr_u32 (&q);
			if (q.err || nm2 < 0 || nm2 > 4096 || nl > size)
			{
				fprintf (f, "material: <truncated>\n");
				continue;
			}
			fprintf (f, "material_anims: %d\n", nm2);
			u32 *units = CALLOC (nm2 ? (size_t)nm2 : 1, sizeof (*units));
			size_t bstart = q.p;
			for (s32 m = 0; m < nm2; m++)
			{
				if (m * 4 + 4 <= nl)
					units[m] = data[q.p + (size_t)m * 4] | (u32)data[q.p + (size_t)m * 4 + 1] << 8
						| (u32)data[q.p + (size_t)m * 4 + 2] << 16
						| (u32)data[q.p + (size_t)m * 4 + 3] << 24;
				char nm[256];
				gfr_blen_str (&q, nm, sizeof (nm));
				if (q.err)
					break;
			}
			(void)bstart;
			FREE (units);
			if (q.err)
				fprintf (f, "material: <truncated names>\n");
		}
		else if (id == 6)
		{
			s32 nm2 = (s32)gfr_u32 (&q);
			u32 nl = gfr_u32 (&q);
			if (q.err || nm2 < 0 || nm2 > 4096 || nl > size)
			{
				fprintf (f, "visibility: <truncated>\n");
				continue;
			}
			fprintf (f, "visibility_anims: %d\n", nm2);
			for (s32 m = 0; m < nm2; m++)
			{
				char nm[256];
				gfr_blen_str (&q, nm, sizeof (nm));
				if (q.err)
					break;
				fprintf (f, "vis[%d]: %s\n", m, nm);
			}
			if (q.err)
				fprintf (f, "visibility: <truncated names>\n");
		}
	}
	return ERR_OK;
}

enumError DecodeGF1Motion_Text (FILE *f, const u8 *data, size_t size)
{
	if (!f)
		return EINVAL;
	if (!IsGF1Motion (data, size))
		return EINVAL;
	u32 count = rd_le32 (data);
	fprintf (f, "# GF1MotionPack (Game Freak XY/ORAS bone motion, SPICA GF1MotionPack)\n");
	fprintf (f, "anims: %u\n", count - 1);
	u32 skel = rd_le32 (data + 4);
	gfr_t r = { data, size, skel, 0 };
	u8 nb = gfr_u8 (&r);
	u8 first = gfr_u8 (&r);
	fprintf (f, "skeleton_bones: %u (first=%u)\n", nb, first);
	if (r.err || !nb)
		return EINVAL;
	u8 *pars = CALLOC (nb, 1);
	if (!pars)
		return ERR_OUT_OF_MEMORY;
	for (u32 i = 1; i < nb; i++)
	{
		pars[i] = gfr_u8 (&r); // parent
		gfr_u8 (&r); // flags
		gfr_u8 (&r); // child count
	}
	// names
	for (u32 i = 1; i < nb && !r.err; i++)
	{
		size_t s = r.p;
		while (r.p < r.n && data[r.p])
			r.p++;
		if (r.p >= r.n)
		{
			r.err = 1;
			break;
		}
		char nm[128];
		size_t L = r.p - s < sizeof (nm) - 1 ? r.p - s : sizeof (nm) - 1;
		memcpy (nm, data + s, L);
		nm[L] = 0;
		r.p++; // NUL
		fprintf (f, "bone[%u]: parent=%u %s\n", i, i == 1 ? 0 : pars[i], nm);
	}
	while ((r.p & 3) && r.p < r.n)
		r.p++;
	// base transforms
	for (u32 i = 0; i < nb && !r.err; i++)
	{
		float t[3], qv[4];
		for (int k = 0; k < 3; k++)
			t[k] = gfr_f32 (&r);
		for (int k = 0; k < 4; k++)
			qv[k] = gfr_f32 (&r);
		if (!r.err)
			fprintf (f, "bind[%u]: t=(%g %g %g) q=(%g %g %g %g)\n", i, t[0], t[1], t[2], qv[0],
				qv[1], qv[2], qv[3]);
	}
	FREE (pars);
	if (r.err)
	{
		fprintf (f, "skeleton: <truncated>\n");
		return ERR_OK;
	}
	for (u32 a = 1; a < count; a++)
	{
		u32 off = rd_le32 (data + 4 + a * 4);
		if (!off)
		{
			fprintf (f, "anim[%u]: <absent>\n", a - 1);
			continue;
		}
		gfr_t q = { data, size, off, 0 };
		u16 noct = gfr_u16 (&q);
		u16 nfr = gfr_u16 (&q);
		if (q.err)
		{
			fprintf (f, "anim[%u]: <truncated>\n", a - 1);
			continue;
		}
		fprintf (f, "anim[%u]: frames=%u octets=%u\n", a - 1, nfr, noct);
	}
	return ERR_OK;
}

enumError DecodeGFModelPack_Text (FILE *f, const u8 *data, size_t size)
{
	if (!f)
		return EINVAL;
	if (!IsGFModelPack (data, size))
		return EINVAL;
	static const char *snames[5] = { "Model", "Texture", "Unknown2", "Unknown3", "Shader" };
	fprintf (f, "# GFModelPack (Game Freak model/texture/shader container)\n");
	size_t tab = 0x18;
	for (int s = 0; s < 5; s++)
	{
		u32 c = rd_le32 (data + 4 + s * 4);
		fprintf (f, "section %s: %u entries\n", snames[s], c);
		for (u32 e = 0; e < c; e++)
		{
			u32 ptr = rd_le32 (data + tab + e * 4);
			gfr_t r = { data, size, ptr, 0 };
			char nm[256];
			gfr_blen_str (&r, nm, sizeof (nm));
			u32 addr = gfr_u32 (&r);
			if (r.err || addr >= size)
			{
				fprintf (f, "  [%u]: <truncated>\n", e);
				continue;
			}
			u32 magic = rd_le32 (data + addr);
			const char *kind = magic == GF_MAGIC_MODEL ? "gfmodel"
				: magic == GF_MAGIC_TEXTURE ? "gftexture"
				: magic == GF_MAGIC_MOTION ? "gfmotion"
											 : "blob";
			fprintf (f, "  [%u]: %s @0x%x (%s)\n", e, nm[0] ? nm : "<noname>", addr, kind);
		}
		tab += (size_t)c * 4;
	}
	return ERR_OK;
}

// ---------------------------------------------------------------------------
// extraction helpers
// ---------------------------------------------------------------------------

static const char *gf_sniff_ext (const u8 *d, size_t n)
{
	if (!d || n < 4)
		return ".bin";
	u32 m = rd_le32 (d);
	if (!memcmp (d, "BCH", 3) && d[3] == 0)
		return ".bch";
	if (!memcmp (d, "CGFX", 4))
		return ".bcres";
	if (m == GF_MAGIC_MODEL)
		return ".gfmodel";
	if (m == GF_MAGIC_TEXTURE)
		return ".gftex";
	if (m == GF_MAGIC_MOTION)
		return ".gfmot";
	if (m == GF_MAGIC_MODELPACK)
		return ".gfpack";
	if (!memcmp (d, "GFLXPACK", 8))
		return ".gflxpack";
	if (!memcmp (d, "SARC", 4))
		return ".sarc";
	if (!memcmp (d, "BNTX", 4))
		return ".bntx";
	if (!memcmp (d, "BNSH", 4))
		return ".bnsh";
	if (!memcmp (d, "MOD", 3) && d[3] == 0)
		return ".mod";
	if (!memcmp (d, "TEX", 3) && d[3] == 0)
		return ".tex";
	if (!memcmp (d, "MRL", 3))
		return ".mrl";
	return ".bin";
}

static void gf_safe_name (char *dst, size_t dstsz, const char *src)
{
	size_t j = 0;
	for (size_t i = 0; src[i] && j + 1 < dstsz; i++)
	{
		char c = src[i];
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<'
			|| c == '>' || c == '|' || (u8)c < 0x20)
			c = '_';
		dst[j++] = c;
	}
	dst[j] = 0;
	if (!j)
		snprintf (dst, dstsz, "file");
}

enumError ExtractGFModelPackArchive (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext_match (arg, ".gfpack") && !is_ext_match (arg, ".bin") && !is_ext_match (arg, ".pack")
		&& !is_ext_match (arg, ".dat"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (!IsGFModelPack (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	u32 total = 0;
	for (int s = 0; s < 5; s++)
		total += rd_le32 (raw + 4 + s * 4);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT GFMODELPACK:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, total, dest);

	size_t tab = 0x18;
	static const char *sect[5] = { "model", "tex", "unk2", "unk3", "shader" };
	for (int s = 0; s < 5; s++)
	{
		u32 c = rd_le32 (raw + 4 + s * 4);
		for (u32 e = 0; e < c; e++)
		{
			u32 ptr = rd_le32 (raw + tab + e * 4);
			gfr_t r = { raw, raw_size, ptr, 0 };
			char nm[256];
			gfr_blen_str (&r, nm, sizeof (nm));
			u32 addr = gfr_u32 (&r);
			if (r.err || addr >= raw_size)
				continue;
			// blob length: next entry's address or EOF
			size_t end = raw_size;
			// scan remaining entries for the smallest address above ours
			{
				size_t t2 = 0x18;
				for (int s2 = 0; s2 < 5 && end > addr; s2++)
				{
					u32 c2 = rd_le32 (raw + 4 + s2 * 4);
					for (u32 e2 = 0; e2 < c2; e2++)
					{
						u32 p2 = rd_le32 (raw + t2 + e2 * 4);
						if (p2 + 2 > raw_size)
							continue;
						gfr_t r2 = { raw, raw_size, p2, 0 };
						char dummy[8];
						gfr_blen_str (&r2, dummy, sizeof (dummy));
						u32 a2 = gfr_u32 (&r2);
						if (!r2.err && a2 > addr && a2 < end)
							end = a2;
					}
					t2 += (size_t)c2 * 4;
				}
			}
			char safe[128];
			gf_safe_name (safe, sizeof (safe), nm[0] ? nm : "file");
			char out_path[PATH_MAX];
			snprintf (out_path, sizeof (out_path), "%s/%s_%s%s", dest, sect[s], safe,
				gf_sniff_ext (raw + addr, end - addr));
			if (!testmode)
				SaveFile (out_path, 0, 0, raw + addr, end - addr, 0);
		}
		tab += (size_t)c * 4;
	}
	FREE (raw);
	return ERR_OK;
}

enumError ExtractGFPackageArchive (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	// Gen6/Gen7 packages are usually extensionless or .bin/.pak inside romfs;
	// the structural gate in IsGFPackage is strict, so accept wide extensions
	// but keep clear of GFLXPACK (handled by its own extractor first).
	if (!is_ext_match (arg, ".bin") && !is_ext_match (arg, ".pak") && !is_ext_match (arg, ".dat")
		&& !is_ext_match (arg, ".gfpkg") && !is_ext_match (arg, ".arc"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (!IsGFPackage (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u32 count = raw[2] | (u32)raw[3] << 8;

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT GFPACKAGE:%s (%u files, magic %.2s) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, (char *)raw, dest);

	for (u32 i = 0; i < count; i++)
	{
		u32 s = rd_le32 (raw + 4 + i * 4);
		u32 e = rd_le32 (raw + 8 + i * 4);
		if (e <= s || e - s > raw_size)
			continue;
		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/file_%04u%s", dest, i,
			gf_sniff_ext (raw + s, e - s));
		if (!testmode)
			SaveFile (out_path, 0, 0, raw + s, e - s, 0);
	}
	FREE (raw);
	return ERR_OK;
}

enumError ExtractGFLXPackArchive (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext_match (arg, ".gflxpack") && !is_ext_match (arg, ".gfpak")
		&& !is_ext_match (arg, ".bin") && !is_ext_match (arg, ".pak"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (!IsGFLXPack (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u32 count = rd_le32 (raw + 0x10);
	u64 info = (u64)rd_le32 (raw + 0x18) | (u64)rd_le32 (raw + 0x1c) << 32;

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT GFLXPACK:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);

	uint done = 0;
	for (u32 i = 0; i < count; i++)
	{
		u64 ep = info + (u64)i * 24;
		if (ep + 24 > raw_size)
			break;
		u32 id = rd_le32 (raw + ep);
		u32 dlen = rd_le32 (raw + ep + 4);
		u32 clen = rd_le32 (raw + ep + 8);
		u64 doff = (u64)rd_le32 (raw + ep + 16) | (u64)rd_le32 (raw + ep + 20) << 32;
		(void)id;
		if (!dlen || dlen > 0x4000000 || !clen || clen > 0x4000000 || doff + clen > raw_size)
			continue;
		u8 *dec = MALLOC (dlen);
		if (!dec)
			continue;
		// raw LZ4 block (no frame header), like SPICA's LZ4.Decompress
		int got = LZ4_decompress_safe ((const char *)raw + doff, (char *)dec, (int)clen, (int)dlen);
		if (got != (int)dlen)
		{
			FREE (dec);
			continue;
		}
		// SPICA sniffs the decompressed magic for the output extension
		const char *ext;
		if (dlen >= 4 && !memcmp (dec, "BNTX", 4))
			ext = ".bntx";
		else if (dlen >= 4 && !memcmp (dec, "BNSH", 4))
			ext = ".bnsh";
		else if (dlen >= 4 && rd_le32 (dec) == 0x20)
			ext = ".gfbmdl";
		else
			ext = gf_sniff_ext (dec, dlen);
		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/file_%04u%s", dest, i, ext);
		if (!testmode)
			SaveFile (out_path, 0, 0, dec, dlen, 0);
		done++;
		FREE (dec);
	}
	FREE (raw);
	// A plain 3DS member table can pass the header probe; only claim the
	// file when at least one LZ4 member validated, otherwise let the xx
	// chain fall through to ExtractGFPAKArchive.
	return done || testmode ? ERR_OK : ERR_NOTHING_TO_DO;
}
