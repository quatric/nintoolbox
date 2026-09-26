// J3D BMD/BDL -- Nintendo GameCube/Wii binary model format.
//
// Decode/encode support with SuperBMD (RenolY2 fork) feature parity at the
// model_t level: geometry, skinning, materials (simplified), textures,
// material/texheader JSON sidecars, profile dump.
//
// Section layout reference: SuperBMDLib/source/BMD/*.cs (RenolY2/SuperBMD).
// GX texture tile codecs: Chadsoft CTools ImageDataFormat (GPLv3, ported).

#include "lib-j3d.h"
#include "lib-image.h"
#include "lib-image-internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef unsigned int uint;

//-----------------------------------------------------------------------------
// Big-endian helpers
//-----------------------------------------------------------------------------

static uint16_t j3d_rd16 (const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] << 8 | (uint16_t)p[1]);
}
static int16_t j3d_rds16 (const uint8_t *p)
{
	return (int16_t)j3d_rd16 (p);
}
static uint32_t j3d_rd32 (const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}
static float j3d_rdf32 (const uint8_t *p)
{
	uint32_t u = j3d_rd32 (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}
static int j3d_ok (const uint8_t *data, size_t size, size_t off, size_t need)
{
	return data && off + need <= size && off + need >= off;
}

// Growable big-endian writer for section assembly.
typedef struct
{
	uint8_t *data;
	size_t size, cap;
} j3d_buf_t;

static void j3d_buf_reserve (j3d_buf_t *b, size_t extra)
{
	if (b->size + extra <= b->cap)
		return;
	size_t nc = b->cap ? b->cap * 2 : 1024;
	while (nc < b->size + extra)
		nc *= 2;
	uint8_t *np = REALLOC (b->data, nc);
	if (!np)
	{
		ASSERT (0);
		exit (ERR_OUT_OF_MEMORY);
	}
	b->data = np;
	b->cap = nc;
}
static void j3d_w8 (j3d_buf_t *b, uint v)
{
	j3d_buf_reserve (b, 1);
	b->data[b->size++] = (uint8_t)v;
}
static void j3d_w16 (j3d_buf_t *b, uint v)
{
	j3d_buf_reserve (b, 2);
	b->data[b->size++] = (uint8_t)(v >> 8);
	b->data[b->size++] = (uint8_t)v;
}
static void j3d_ws16 (j3d_buf_t *b, int v)
{
	j3d_w16 (b, (uint)(v & 0xffff));
}
static void j3d_w32 (j3d_buf_t *b, uint32_t v)
{
	j3d_buf_reserve (b, 4);
	b->data[b->size++] = (uint8_t)(v >> 24);
	b->data[b->size++] = (uint8_t)(v >> 16);
	b->data[b->size++] = (uint8_t)(v >> 8);
	b->data[b->size++] = (uint8_t)v;
}
static void j3d_wf32 (j3d_buf_t *b, float f)
{
	uint32_t u;
	memcpy (&u, &f, 4);
	j3d_w32 (b, u);
}
static void j3d_wbytes (j3d_buf_t *b, const void *src, size_t n)
{
	j3d_buf_reserve (b, n);
	memcpy (b->data + b->size, src, n);
	b->size += n;
}
static void j3d_wpad (j3d_buf_t *b, size_t align, uint8_t fill)
{
	while (b->size & (align - 1))
		j3d_w8 (b, fill);
}
static void j3d_patch32 (j3d_buf_t *b, size_t off, uint32_t v)
{
	b->data[off] = (uint8_t)(v >> 24);
	b->data[off + 1] = (uint8_t)(v >> 16);
	b->data[off + 2] = (uint8_t)(v >> 8);
	b->data[off + 3] = (uint8_t)v;
}
static void j3d_free_buf (j3d_buf_t *b)
{
	FREE (b->data);
	b->data = 0;
	b->size = b->cap = 0;
}

//-----------------------------------------------------------------------------
// Detection + section scan
//-----------------------------------------------------------------------------

int IsJ3D (const uint8_t *data, size_t size)
{
	if (!j3d_ok (data, size, 0, 32))
		return 0;
	if (memcmp (data, "J3D2", 4))
		return 0;
	return !memcmp (data + 4, "bmd3", 4) || !memcmp (data + 4, "bmd2", 4)
		|| !memcmp (data + 4, "bdl4", 4);
}
int IsJ3DBMD (const uint8_t *data, size_t size)
{
	if (!j3d_ok (data, size, 0, 32))
		return 0;
	if (memcmp (data, "J3D2", 4))
		return 0;
	return !memcmp (data + 4, "bmd3", 4) || !memcmp (data + 4, "bmd2", 4);
}
int IsJ3DBDL (const uint8_t *data, size_t size)
{
	if (!j3d_ok (data, size, 0, 32))
		return 0;
	return !memcmp (data, "J3D2bdl4", 8);
}

// Locate every top-level section by magic; returns 1 on sane header.
static int j3d_find_sections (const uint8_t *data, size_t size, size_t *inf1, size_t *vtx1,
	size_t *evp1, size_t *drw1, size_t *jnt1, size_t *shp1, size_t *mat3, size_t *mdl3,
	size_t *tex1, int *is_bdl)
{
	*inf1 = *vtx1 = *evp1 = *drw1 = *jnt1 = *shp1 = *mat3 = *mdl3 = *tex1 = 0;
	if (!IsJ3D (data, size))
		return 0;
	if (is_bdl)
		*is_bdl = !memcmp (data + 4, "bdl4", 4);
	size_t pos = 32; // header (8 magic + size + count) + 16 version + 16 dummy
	while (j3d_ok (data, size, pos, 8))
	{
		uint32_t sect_size = j3d_rd32 (data + pos + 4);
		if (sect_size < 8 || pos + sect_size > size || pos + sect_size < pos)
			break;
		if (!memcmp (data + pos, "INF1", 4))
			*inf1 = pos;
		else if (!memcmp (data + pos, "VTX1", 4))
			*vtx1 = pos;
		else if (!memcmp (data + pos, "EVP1", 4))
			*evp1 = pos;
		else if (!memcmp (data + pos, "DRW1", 4))
			*drw1 = pos;
		else if (!memcmp (data + pos, "JNT1", 4))
			*jnt1 = pos;
		else if (!memcmp (data + pos, "SHP1", 4))
			*shp1 = pos;
		else if (!memcmp (data + pos, "MAT3", 4) || !memcmp (data + pos, "MAT4", 4))
			*mat3 = pos;
		else if (!memcmp (data + pos, "MDL3", 4))
			*mdl3 = pos;
		else if (!memcmp (data + pos, "TEX1", 4))
			*tex1 = pos;
		pos += sect_size;
	}
	return *inf1 && *vtx1 && *jnt1 && *shp1 && *mat3 && *tex1;
}

// J3D name table: s16 count, s16 -1, per entry [u16 hash, s16 stroff]
// with offsets relative to table start, then NUL strings.
static int j3d_read_nametable (
	const uint8_t *data, size_t size, size_t tab, char ***out_names, int *out_count)
{
	*out_names = 0;
	*out_count = 0;
	if (!j3d_ok (data, size, tab, 4))
		return 0;
	int n = j3d_rds16 (data + tab);
	if (n < 0 || n > 4096)
		return 0;
	if (!j3d_ok (data, size, tab, (size_t)4 + (size_t)n * 4))
		return 0;
	char **names = CALLOC ((size_t)n ? (size_t)n : 1, sizeof (*names));
	if (!names)
		return 0;
	for (int i = 0; i < n; i++)
	{
		int stroff = j3d_rds16 (data + tab + 4 + (size_t)i * 4 + 2);
		size_t sp = tab + (size_t)stroff;
		size_t len = 0;
		while (j3d_ok (data, size, sp + len, 1) && data[sp + len] && len < 256)
			len++;
		names[i] = MALLOC (len + 1);
		if (!names[i])
		{
			for (int k = 0; k < i; k++)
				FREE (names[k]);
			FREE (names);
			return 0;
		}
		for (size_t k = 0; k < len; k++)
			names[i][k] = (char)data[sp + k];
		names[i][len] = 0;
	}
	*out_names = names;
	*out_count = n;
	return 1;
}
static void j3d_free_names (char **names, int n)
{
	if (!names)
		return;
	for (int i = 0; i < n; i++)
		FREE (names[i]);
	FREE (names);
}
static uint16_t j3d_hash_str (const char *s)
{
	uint16_t h = 0;
	for (; *s; s++)
		h = (uint16_t)(h * 3 + (uint8_t)*s);
	return h;
}
static void j3d_write_nametable (j3d_buf_t *b, char (*names)[64], int n)
{
	size_t start = b->size;
	j3d_ws16 (b, n);
	j3d_ws16 (b, -1);
	size_t *patch = CALLOC ((size_t)n ? (size_t)n : 1, sizeof (*patch));
	for (int i = 0; i < n; i++)
	{
		j3d_w16 (b, names ? j3d_hash_str (names[i]) : 0);
		patch[i] = b->size;
		j3d_w16 (b, 0);
	}
	for (int i = 0; i < n; i++)
	{
		size_t cur = b->size;
		uint16_t rel = (uint16_t)(cur - start);
		b->data[patch[i]] = (uint8_t)(rel >> 8);
		b->data[patch[i] + 1] = (uint8_t)rel;
		const char *s = names ? names[i] : "";
		j3d_wbytes (b, s, strlen (s) + 1);
	}
	FREE (patch);
}

//-----------------------------------------------------------------------------
// GX texture codecs (ported from Chadsoft CTools ImageDataFormat, GPLv3;
// CMPR sub-block math from SuperBMD BinaryTextureImage, same lineage)
//-----------------------------------------------------------------------------

typedef enum
{
	J3D_TEX_I4 = 0,
	J3D_TEX_I8 = 1,
	J3D_TEX_IA4 = 2,
	J3D_TEX_IA8 = 3,
	J3D_TEX_RGB565 = 4,
	J3D_TEX_RGB5A3 = 5,
	J3D_TEX_RGBA32 = 6,
	J3D_TEX_C4 = 8,
	J3D_TEX_C8 = 9,
	J3D_TEX_C14X2 = 0x0a,
	J3D_TEX_CMPR = 0x0e
} j3d_texfmt_t;

typedef enum
{
	J3D_PAL_IA8 = 0,
	J3D_PAL_RGB565 = 1,
	J3D_PAL_RGB5A3 = 2
} j3d_palfmt_t;

static void j3d_px_rgb565 (uint16_t px, uint8_t *o)
{
	uint r = (px >> 11) & 0x1f, g = (px >> 5) & 0x3f, b = px & 0x1f;
	o[0] = (uint8_t)(r << 3 | r >> 2);
	o[1] = (uint8_t)(g << 2 | g >> 4);
	o[2] = (uint8_t)(b << 3 | b >> 2);
	o[3] = 255;
}
static void j3d_px_rgb5a3 (uint16_t px, uint8_t *o)
{
	if (px & 0x8000)
	{
		uint r = (px >> 10) & 0x1f, g = (px >> 5) & 0x1f, b = px & 0x1f;
		o[0] = (uint8_t)(r << 3 | r >> 2);
		o[1] = (uint8_t)(g << 3 | g >> 2);
		o[2] = (uint8_t)(b << 3 | b >> 2);
		o[3] = 255;
	}
	else
	{
		uint a = (px >> 12) & 7, r = (px >> 8) & 0xf, g = (px >> 4) & 0xf, b = px & 0xf;
		o[0] = (uint8_t)(r << 4 | r);
		o[1] = (uint8_t)(g << 4 | g);
		o[2] = (uint8_t)(b << 4 | b);
		o[3] = (uint8_t)(a << 5 | a << 2 | a >> 1);
	}
}
static void j3d_px_pal (int idx, const uint8_t *pal, int palfmt, uint8_t *o)
{
	if (palfmt == J3D_PAL_IA8)
	{
		o[0] = o[1] = o[2] = pal[idx * 2 + 1];
		o[3] = pal[idx * 2];
	}
	else if (palfmt == J3D_PAL_RGB565)
		j3d_px_rgb565 ((uint16_t)(pal[idx * 2] << 8 | pal[idx * 2 + 1]), o);
	else
		j3d_px_rgb5a3 ((uint16_t)(pal[idx * 2] << 8 | pal[idx * 2 + 1]), o);
}

// Tiled GX decode: walks blocks in raster order, pixels inside each block
// in raster order, skipping (but consuming) out-of-bounds padding.
static uint8_t *j3d_tex_decode (const uint8_t *src, size_t src_size, size_t *pos, uint w, uint h,
	int fmt, const uint8_t *pal, int palfmt, int palcount)
{
	if (!w || !h || w > 4096 || h > 4096)
		return 0;
	uint8_t *out = CALLOC ((size_t)w * h, 4);
	if (!out)
		return 0;
#define J3D_NEED(n)                                                                                \
	do                                                                                             \
	{                                                                                              \
		if (*pos + (n) > src_size)                                                                 \
		{                                                                                          \
			FREE (out);                                                                            \
			return 0;                                                                              \
		}                                                                                          \
	} while (0)
#define J3D_PUT(x, y, r, g, b, a)                                                                  \
	do                                                                                             \
	{                                                                                              \
		if ((x) < w && (y) < h)                                                                    \
		{                                                                                          \
			uint8_t *d = out + ((size_t)(y) * w + (x)) * 4;                                        \
			d[0] = (r);                                                                            \
			d[1] = (g);                                                                            \
			d[2] = (b);                                                                            \
			d[3] = (a);                                                                            \
		}                                                                                          \
	} while (0)

	uint bw = 0, bh = 0;
	switch (fmt)
	{
		case J3D_TEX_I4:
			bw = 8;
			bh = 8;
			break;
		case J3D_TEX_I8:
		case J3D_TEX_IA4:
			bw = 8;
			bh = 4;
			break;
		case J3D_TEX_IA8:
		case J3D_TEX_RGB565:
		case J3D_TEX_RGB5A3:
		case J3D_TEX_RGBA32:
			bw = 4;
			bh = 4;
			break;
		case J3D_TEX_C4:
			bw = 8;
			bh = 8;
			break;
		case J3D_TEX_C8:
			bw = 8;
			bh = 4;
			break;
		case J3D_TEX_CMPR:
			bw = 8;
			bh = 8;
			break;
		default:
			FREE (out);
			return 0;
	}
	for (uint by = 0; by < h; by += bh)
		for (uint bx = 0; bx < w; bx += bw)
		{
			if (fmt == J3D_TEX_CMPR)
			{
				for (int sy = 0; sy < 2; sy++)
					for (int sx = 0; sx < 2; sx++)
					{
						J3D_NEED (8);
						uint16_t c1 = (uint16_t)(src[*pos] << 8 | src[*pos + 1]);
						uint16_t c2 = (uint16_t)(src[*pos + 2] << 8 | src[*pos + 3]);
						uint32_t bits = (uint32_t)src[*pos + 4] << 24
							| (uint32_t)src[*pos + 5] << 16 | (uint32_t)src[*pos + 6] << 8
							| src[*pos + 7];
						*pos += 8;
						uint8_t ct[4][4];
						j3d_px_rgb565 (c1, ct[0]);
						j3d_px_rgb565 (c2, ct[1]);
						if (c1 > c2)
						{
							for (int c = 0; c < 3; c++)
							{
								ct[2][c] = (uint8_t)((2 * ct[0][c] + ct[1][c]) / 3);
								ct[3][c] = (uint8_t)((ct[0][c] + 2 * ct[1][c]) / 3);
							}
							ct[2][3] = ct[3][3] = 255;
						}
						else
						{
							for (int c = 0; c < 3; c++)
								ct[2][c] = (uint8_t)((ct[0][c] + ct[1][c]) / 2);
							ct[2][3] = 255;
							ct[3][0] = ct[3][1] = ct[3][2] = 0;
							ct[3][3] = 0;
						}
						for (int i = 0; i < 16; i++)
						{
							int sel = (bits >> ((15 - i) * 2)) & 3;
							J3D_PUT (bx + (uint)sx * 4 + (uint)(i % 4),
								by + (uint)sy * 4 + (uint)(i / 4), ct[sel][0], ct[sel][1],
								ct[sel][2], ct[sel][3]);
						}
					}
			}
			else if (fmt == J3D_TEX_RGBA32)
			{
				// two 4x4 sub-blocks: AR then GB
				for (int pass = 0; pass < 2; pass++)
					for (uint py = 0; py < 4; py++)
						for (uint px = 0; px < 4; px++)
						{
							J3D_NEED (2);
							uint8_t b0 = src[(*pos)++], b1 = src[(*pos)++];
							uint x = bx + px, y = by + py;
							if (x < w && y < h)
							{
								uint8_t *d = out + ((size_t)y * w + x) * 4;
								if (!pass)
								{
									d[3] = b0;
									d[0] = b1;
								}
								else
								{
									d[1] = b0;
									d[2] = b1;
								}
							}
						}
			}
			else if (fmt == J3D_TEX_C4 || fmt == J3D_TEX_C8)
			{
				uint ph = fmt == J3D_TEX_C4 ? 8 : 4;
				for (uint py = 0; py < ph; py++)
					for (uint px = 0; px < 8; px += (fmt == J3D_TEX_C4 ? 2 : 1))
					{
						if (fmt == J3D_TEX_C4)
						{
							J3D_NEED (1);
							uint8_t v = src[(*pos)++];
							uint8_t idx[2] = { (uint8_t)(v >> 4), (uint8_t)(v & 15) };
							for (int k = 0; k < 2; k++)
							{
								uint8_t rgba[4] = { 0, 0, 0, 0 };
								if (pal && idx[k] < palcount)
									j3d_px_pal (idx[k], pal, palfmt, rgba);
								J3D_PUT (
									bx + px + (uint)k, by + py, rgba[0], rgba[1], rgba[2], rgba[3]);
							}
						}
						else
						{
							J3D_NEED (1);
							uint8_t idx = src[(*pos)++];
							uint8_t rgba[4] = { 0, 0, 0, 0 };
							if (pal && idx < palcount)
								j3d_px_pal (idx, pal, palfmt, rgba);
							J3D_PUT (bx + px, by + py, rgba[0], rgba[1], rgba[2], rgba[3]);
						}
					}
			}
			else
			{
				for (uint py = 0; py < bh; py++)
					for (uint px = 0; px < bw; px++)
					{
						uint x = bx + px, y = by + py;
						uint8_t rgba[4] = { 0, 0, 0, 255 };
						if (fmt == J3D_TEX_I4)
						{
							if (!(px & 1))
							{
								J3D_NEED (1);
								uint8_t v = src[(*pos)++];
								rgba[0] = rgba[1] = rgba[2] = rgba[3] = (uint8_t)((v >> 4) * 0x11);
								J3D_PUT (x, y, rgba[0], rgba[1], rgba[2], rgba[3]);
								rgba[0] = rgba[1] = rgba[2] = rgba[3] = (uint8_t)((v & 15) * 0x11);
								J3D_PUT (x + 1, y, rgba[0], rgba[1], rgba[2], rgba[3]);
							}
						}
						else if (fmt == J3D_TEX_I8)
						{
							J3D_NEED (1);
							uint8_t v = src[(*pos)++];
							J3D_PUT (x, y, v, v, v, v);
						}
						else if (fmt == J3D_TEX_IA4)
						{
							J3D_NEED (1);
							uint8_t v = src[(*pos)++];
							uint8_t l = (uint8_t)((v & 15) * 0x11), a = (uint8_t)((v >> 4) * 0x11);
							J3D_PUT (x, y, l, l, l, a);
						}
						else if (fmt == J3D_TEX_IA8)
						{
							J3D_NEED (2);
							uint8_t a = src[(*pos)++], l = src[(*pos)++];
							J3D_PUT (x, y, l, l, l, a);
						}
						else
						{
							J3D_NEED (2);
							uint16_t v = (uint16_t)(src[*pos] << 8 | src[*pos + 1]);
							*pos += 2;
							if (fmt == J3D_TEX_RGB565)
								j3d_px_rgb565 (v, rgba);
							else
								j3d_px_rgb5a3 (v, rgba);
							J3D_PUT (x, y, rgba[0], rgba[1], rgba[2], rgba[3]);
						}
					}
			}
		}
#undef J3D_NEED
#undef J3D_PUT
	return out;
}

// Encode RGBA bytes to GX tiles. fmt is J3D_TEX_RGBA32 or J3D_TEX_CMPR
// (CMPR ported from CTools ConvertBlockToQuaterCmpr).
static int j3d_cmpr_dist (const uint8_t *a, const uint8_t *b)
{
	int t = 0;
	for (int i = 0; i < 3; i++)
	{
		int v = (int)a[i] - (int)b[i];
		t += v * v;
	}
	return t;
}
static int j3d_cmpr_best (uint8_t pal[4][4], const uint8_t *px)
{
	if (px[3] < 8)
		return 3;
	int best = 0, bd = 0x7fffffff;
	for (int i = 0; i < 4; i++)
	{
		if (pal[i][3] != 255)
			break;
		int d = j3d_cmpr_dist (pal[i], px);
		if (!d)
			return i;
		if (d < bd)
		{
			bd = d;
			best = i;
		}
	}
	return best;
}
static void j3d_cmpr_block (const uint8_t blk[64], uint8_t out[8])
{
	int col1 = -1, col2 = -1, dist = -1, alpha = 0;
	for (int i = 0; i < 15; i++)
	{
		if (blk[i * 4 + 3] < 16)
			alpha = 1;
		else
			for (int j = i + 1; j < 16; j++)
			{
				int t = j3d_cmpr_dist (blk + i * 4, blk + j * 4);
				if (t > dist)
				{
					dist = t;
					col1 = i;
					col2 = j;
				}
			}
	}
	uint8_t pal[4][4];
	if (dist < 0)
	{
		pal[0][0] = pal[0][1] = pal[0][2] = 0;
		pal[0][3] = 255;
		pal[1][0] = pal[1][1] = pal[1][2] = 255;
		pal[1][3] = 255;
	}
	else
	{
		memcpy (pal[0], blk + col1 * 4, 3);
		pal[0][3] = 255;
		memcpy (pal[1], blk + col2 * 4, 3);
		pal[1][3] = 255;
		if ((pal[0][0] >> 3) == (pal[1][0] >> 3) && (pal[0][1] >> 2) == (pal[1][1] >> 2)
			&& (pal[0][2] >> 3) == (pal[1][2] >> 3))
		{
			if (!pal[0][0] && !pal[0][1] && !pal[0][2])
				pal[1][0] = pal[1][1] = pal[1][2] = 255;
			else
				pal[1][0] = pal[1][1] = pal[1][2] = 0;
		}
	}
	// RGB565 endpoints, big-endian byte order per u16
	uint16_t e0 = (uint16_t)(((pal[0][0] >> 3) << 11) | ((pal[0][1] >> 2) << 5) | (pal[0][2] >> 3));
	uint16_t e1 = (uint16_t)(((pal[1][0] >> 3) << 11) | ((pal[1][1] >> 2) << 5) | (pal[1][2] >> 3));
	out[0] = (uint8_t)(e0 >> 8);
	out[1] = (uint8_t)e0;
	out[2] = (uint8_t)(e1 >> 8);
	out[3] = (uint8_t)e1;
	if (((e0 > e1 || (e0 == e1 && out[1] >= out[3])) == alpha))
	{
		out[4] = out[0];
		out[5] = out[1];
		out[0] = out[2];
		out[1] = out[3];
		out[2] = out[4];
		out[3] = out[5];
		uint8_t t[4];
		memcpy (t, pal[0], 4);
		memcpy (pal[0], pal[1], 4);
		memcpy (pal[1], t, 4);
	}
	if (!alpha)
	{
		for (int c = 0; c < 3; c++)
		{
			pal[2][c] = (uint8_t)(((pal[0][c] << 1) + pal[1][c]) / 3);
			pal[3][c] = (uint8_t)((pal[0][c] + (pal[1][c] << 1)) / 3);
		}
		pal[2][3] = pal[3][3] = 255;
	}
	else
	{
		for (int c = 0; c < 3; c++)
			pal[2][c] = (uint8_t)((pal[0][c] + pal[1][c]) >> 1);
		pal[2][3] = 255;
		pal[3][0] = pal[3][1] = pal[3][2] = 0;
		pal[3][3] = 0;
	}
	for (int i = 0; i < 4; i++)
		out[4 + i] = (uint8_t)(j3d_cmpr_best (pal, blk + i * 16) << 6
			| j3d_cmpr_best (pal, blk + i * 16 + 4) << 4
			| j3d_cmpr_best (pal, blk + i * 16 + 8) << 2 | j3d_cmpr_best (pal, blk + i * 16 + 12));
}
static void j3d_tex_encode (j3d_buf_t *b, const uint8_t *rgba, uint w, uint h, int fmt)
{
	if (fmt == J3D_TEX_RGBA32)
	{
		uint nbw = (w + 3) / 4, nbh = (h + 3) / 4;
		for (uint by = 0; by < nbh; by++)
			for (uint bx = 0; bx < nbw; bx++)
				for (int pass = 0; pass < 2; pass++)
					for (uint py = 0; py < 4; py++)
						for (uint px = 0; px < 4; px++)
						{
							uint x = bx * 4 + px, y = by * 4 + py;
							uint8_t r = 0, g = 0, bl = 0, a = 0;
							if (x < w && y < h)
							{
								const uint8_t *s = rgba + ((size_t)y * w + x) * 4;
								r = s[0];
								g = s[1];
								bl = s[2];
								a = s[3];
							}
							if (!pass)
							{
								j3d_w8 (b, a);
								j3d_w8 (b, r);
							}
							else
							{
								j3d_w8 (b, g);
								j3d_w8 (b, bl);
							}
						}
		return;
	}
	// CMPR: 8x8 blocks of four 4x4 sub-blocks
	uint8_t blk[256], sub[64];
	for (uint by = 0; by < h; by += 8)
		for (uint bx = 0; bx < w; bx += 8)
		{
			memset (blk, 0, sizeof (blk));
			for (uint py = 0; py < 8 && by + py < h; py++)
				for (uint px = 0; px < 8 && bx + px < w; px++)
					memcpy (
						blk + (py * 8 + px) * 4, rgba + (((size_t)(by + py) * w) + bx + px) * 4, 4);
			for (int i = 0, x = 0, y = 0; i < 4; i++)
			{
				memcpy (sub, blk + x + y, 16);
				memcpy (sub + 16, blk + x + y + 32, 16);
				memcpy (sub + 32, blk + x + y + 64, 16);
				memcpy (sub + 48, blk + x + y + 96, 16);
				x = 16 - x;
				if (!x)
					y = 128;
				uint8_t enc[8];
				j3d_cmpr_block (sub, enc);
				j3d_wbytes (b, enc, 8);
			}
		}
}

//-----------------------------------------------------------------------------
// Decode-time model assembly
//-----------------------------------------------------------------------------

// GX vertex attribute ids (SuperBMD GXVertexAttribute)
enum
{
	J3D_ATTR_PMTX = 0,
	J3D_ATTR_POS = 9,
	J3D_ATTR_NRM = 10,
	J3D_ATTR_C0 = 11,
	J3D_ATTR_C1 = 12,
	J3D_ATTR_T0 = 13, // +0..7
	J3D_ATTR_NBT = 25,
	J3D_ATTR_NULL = 255
};
// GX primitive types
enum
{
	J3D_PRIM_TRI = 0x90,
	J3D_PRIM_STRIP = 0x98,
	J3D_PRIM_FAN = 0xa0
};
// INF1 node types
enum
{
	J3D_NODE_TERM = 0,
	J3D_NODE_OPEN = 1,
	J3D_NODE_CLOSE = 2,
	J3D_NODE_JOINT = 16,
	J3D_NODE_MAT = 17,
	J3D_NODE_SHAPE = 18
};

typedef struct
{
	float *v; // flat xyz or xyzw or uv
	size_t n;
	int comps;
} j3d_pool_t;

typedef struct
{
	// INF1
	int16_t *node_type;
	int16_t *node_idx;
	int num_nodes;
	// VTX1 pools
	j3d_pool_t pos, nrm, c0, c1, tex[8];
	int has_nrm, has_c[2], has_tex[8];
	// EVP1
	float *evp_weights; // flat per-entry counts in evp_wcount
	uint16_t *evp_bones;
	int *evp_wcount;
	int num_evp;
	float *evp_inv; // num_joints_file x 12
	int num_inv;
	// DRW1
	uint8_t *drw_weighted;
	uint16_t *drw_idx;
	int num_drw;
	// JNT1
	char **jnames;
	int num_joints_file; // FlatSkeleton order (remap applied)
	float *jscale; // x3
	float *jeuler; // degrees x3
	float *jtrans; // x3
	int *jparent; // INF1-derived, -1 root
	int *jmtxtype;
	// MAT3 (simplified)
	char **mnames;
	int num_mats;
	float *mdiffuse; // x4
	int (*mtex)[8]; // TEX1 indices, -1 absent
	uint8_t *mcull; // 0..3
	// TEX1
	char **tnames;
	int num_tex;
	uint8_t *tfmt;
	uint8_t *twrap_s, *twrap_t;
	uint8_t *tminf, *tmagf;
	uint8_t **trgba; // decoded base level
	uint16_t *tw, *th;
	uint8_t *tmips; // extra level count
	uint8_t ***tmip_rgba;
	uint16_t **tmip_w, **tmip_h;
} j3d_dec_t;

static void j3d_pool_free (j3d_pool_t *p)
{
	FREE (p->v);
	p->v = 0;
	p->n = 0;
}
static void j3d_dec_free (j3d_dec_t *d)
{
	FREE (d->node_type);
	FREE (d->node_idx);
	j3d_pool_free (&d->pos);
	j3d_pool_free (&d->nrm);
	j3d_pool_free (&d->c0);
	j3d_pool_free (&d->c1);
	for (int i = 0; i < 8; i++)
		j3d_pool_free (&d->tex[i]);
	FREE (d->evp_weights);
	FREE (d->evp_bones);
	FREE (d->evp_wcount);
	FREE (d->evp_inv);
	FREE (d->drw_weighted);
	FREE (d->drw_idx);
	j3d_free_names (d->jnames, d->num_joints_file);
	FREE (d->jscale);
	FREE (d->jeuler);
	FREE (d->jtrans);
	FREE (d->jparent);
	FREE (d->jmtxtype);
	j3d_free_names (d->mnames, d->num_mats);
	FREE (d->mdiffuse);
	FREE (d->mtex);
	FREE (d->mcull);
	j3d_free_names (d->tnames, d->num_tex);
	FREE (d->tfmt);
	FREE (d->twrap_s);
	FREE (d->twrap_t);
	FREE (d->tminf);
	FREE (d->tmagf);
	FREE (d->tw);
	FREE (d->th);
	if (d->trgba)
	{
		for (int i = 0; i < d->num_tex; i++)
		{
			FREE (d->trgba[i]);
			if (d->tmip_rgba && d->tmip_rgba[i])
			{
				for (int m = 0; m < d->tmips[i]; m++)
					FREE (d->tmip_rgba[i][m]);
				FREE (d->tmip_rgba[i]);
				FREE (d->tmip_w[i]);
				FREE (d->tmip_h[i]);
			}
		}
		FREE (d->trgba);
		FREE (d->tmip_rgba);
		FREE (d->tmip_w);
		FREE (d->tmip_h);
	}
	FREE (d->tmips);
	memset (d, 0, sizeof (*d));
}

// --- INF1 ---
static int j3d_parse_inf1 (const uint8_t *data, size_t size, size_t sect, j3d_dec_t *d)
{
	if (!j3d_ok (data, size, sect, 32))
		return 0;
	uint32_t ssize = j3d_rd32 (data + sect + 4);
	size_t end = sect + ssize;
	size_t p = sect + 24; // nodes start (magic+size+unk+pkts+verts+hieroff)
	int cap = 64;
	d->node_type = MALLOC ((size_t)cap * 2);
	d->node_idx = MALLOC ((size_t)cap * 2);
	d->num_nodes = 0;
	if (!d->node_type || !d->node_idx)
		return 0;
	for (;;)
	{
		if (!j3d_ok (data, size, p, 4) || p + 4 > end)
			break;
		int t = j3d_rds16 (data + p), idx = j3d_rds16 (data + p + 2);
		p += 4;
		if (d->num_nodes >= cap)
		{
			cap *= 2;
			d->node_type = REALLOC (d->node_type, (size_t)cap * 2);
			d->node_idx = REALLOC (d->node_idx, (size_t)cap * 2);
			if (!d->node_type || !d->node_idx)
				return 0;
		}
		d->node_type[d->num_nodes] = (int16_t)t;
		d->node_idx[d->num_nodes] = (int16_t)idx;
		d->num_nodes++;
		if (t == J3D_NODE_TERM || d->num_nodes > 100000)
			break;
	}
	return d->num_nodes > 0;
}

// --- VTX1 ---
static float j3d_vtx_val (const uint8_t *data, size_t size, size_t *p, int type, int frac, int *ok)
{
	*ok = 0;
	if (type == 4) // Float32
	{
		if (!j3d_ok (data, size, *p, 4))
			return 0;
		*p += 4;
		*ok = 1;
		return j3d_rdf32 (data + *p - 4);
	}
	float div = (float)(1 << (frac & 31));
	if (type == 0) // U8
	{
		if (!j3d_ok (data, size, *p, 1))
			return 0;
		*ok = 1;
		return (float)data[(*p)++] / div;
	}
	if (type == 1) // S8
	{
		if (!j3d_ok (data, size, *p, 1))
			return 0;
		*ok = 1;
		return (float)(int8_t)data[(*p)++] / div;
	}
	if (type == 2) // U16
	{
		if (!j3d_ok (data, size, *p, 2))
			return 0;
		uint v = j3d_rd16 (data + *p);
		*p += 2;
		*ok = 1;
		return (float)v / div;
	}
	if (type == 3) // S16
	{
		if (!j3d_ok (data, size, *p, 2))
			return 0;
		int v = j3d_rds16 (data + *p);
		*p += 2;
		*ok = 1;
		return (float)v / div;
	}
	return 0;
}
static int j3d_parse_vtx1 (const uint8_t *data, size_t size, size_t sect, j3d_dec_t *d)
{
	if (!j3d_ok (data, size, sect, 0x40))
		return 0;
	uint32_t ssize = j3d_rd32 (data + sect + 4);
	uint32_t hdr = j3d_rd32 (data + sect + 8);
	uint32_t offs[13];
	for (int i = 0; i < 13; i++)
		offs[i] = j3d_rd32 (data + sect + 12 + (size_t)i * 4);
	size_t hp = sect + hdr;
	for (;;)
	{
		if (!j3d_ok (data, size, hp, 12))
			return 0;
		int attr = (int)j3d_rd32 (data + hp);
		int comp = (int)j3d_rd32 (data + hp + 4);
		int type = (int)j3d_rd32 (data + hp + 8);
		int frac = data[hp + 12];
		hp += 16;
		if (attr == J3D_ATTR_NULL)
			break;
		int slot = -1, comps = 0;
		j3d_pool_t *pool = 0;
		if (attr == J3D_ATTR_POS)
		{
			pool = &d->pos;
			comps = comp == 0 ? 2 : 3;
		}
		else if (attr == J3D_ATTR_NRM)
		{
			if (comp != 0)
				continue; // NBT unsupported
			pool = &d->nrm;
			d->has_nrm = 1;
			comps = 3;
		}
		else if (attr == J3D_ATTR_C0 || attr == J3D_ATTR_C1)
		{
			slot = attr - J3D_ATTR_C0;
			pool = slot ? &d->c1 : &d->c0;
			d->has_c[slot] = 1;
			comps = 4;
		}
		else if (attr >= J3D_ATTR_T0 && attr <= J3D_ATTR_T0 + 7)
		{
			slot = attr - J3D_ATTR_T0;
			pool = &d->tex[slot];
			d->has_tex[slot] = 1;
			comps = comp == 0 ? 1 : 2;
		}
		else
			continue;
		int oidx = -1;
		if (attr == J3D_ATTR_POS)
			oidx = 0;
		else if (attr == J3D_ATTR_NRM)
			oidx = 1;
		else if (attr == J3D_ATTR_C0)
			oidx = 3;
		else if (attr == J3D_ATTR_C1)
			oidx = 4;
		else
			oidx = 5 + slot;
		if (oidx < 0 || !offs[oidx])
			continue;
		// span until next nonzero offset
		uint32_t start = offs[oidx], stop = ssize;
		for (int i = oidx + 1; i < 13; i++)
			if (offs[i] && offs[i] > start)
			{
				stop = offs[i];
				break;
			}
		size_t count = 0;
		if (pool == &d->c0 || pool == &d->c1)
		{
			int stride = type == 0 || type == 3 ? 2 : 4; // RGB565/RGBA4 vs RGBA8-ish
			count = stop > start ? (stop - start) / (size_t)stride : 0;
		}
		else
		{
			int stride = type <= 1 ? 1 : type <= 3 ? 2 : 4;
			count = (stop > start && comps) ? (stop - start) / ((size_t)comps * (size_t)stride) : 0;
		}
		if (!count || count > 1000000)
			continue;
		pool->v = CALLOC (count * (size_t)(comps == 4 ? 4 : 3), sizeof (float));
		if (!pool->v)
			return 0;
		pool->comps = comps == 4 ? 4 : 3;
		size_t p = sect + start;
		for (size_t i = 0; i < count; i++)
		{
			if (pool == &d->c0 || pool == &d->c1)
			{
				float r = 0, g = 0, b = 0, a = 1;
				if (type == 0) // RGB565
				{
					if (!j3d_ok (data, size, p, 2))
						break;
					uint16_t v = j3d_rd16 (data + p);
					p += 2;
					r = (float)((v >> 11) & 31) / 31;
					g = (float)((v >> 5) & 63) / 63;
					b = (float)(v & 31) / 31;
				}
				else if (type == 3) // RGBA4
				{
					if (!j3d_ok (data, size, p, 2))
						break;
					uint16_t v = j3d_rd16 (data + p);
					p += 2;
					r = (float)((v >> 12) & 15) / 15;
					g = (float)((v >> 8) & 15) / 15;
					b = (float)((v >> 4) & 15) / 15;
					a = (float)(v & 15) / 15;
				}
				else if (type == 4) // RGBA6 (packed u32)
				{
					if (!j3d_ok (data, size, p, 4))
						break;
					uint32_t v = j3d_rd32 (data + p);
					p += 4;
					r = (float)((v >> 18) & 63) / 63;
					g = (float)((v >> 12) & 63) / 63;
					b = (float)((v >> 6) & 63) / 63;
					a = (float)(v & 63) / 63;
				}
				else // RGBA8 (also RGB8/RGBX8: 4 bytes, ignore 4th)
				{
					if (!j3d_ok (data, size, p, 4))
						break;
					r = (float)data[p] / 255;
					g = (float)data[p + 1] / 255;
					b = (float)data[p + 2] / 255;
					a = (float)data[p + 3] / 255;
					p += 4;
				}
				pool->v[i * 4] = r;
				pool->v[i * 4 + 1] = g;
				pool->v[i * 4 + 2] = b;
				pool->v[i * 4 + 3] = a;
			}
			else
			{
				float v0 = 0, v1 = 0, v2 = 0;
				int ok = 1, bad = 0;
				v0 = j3d_vtx_val (data, size, &p, type, frac, &ok);
				bad |= !ok;
				if (comps >= 2)
				{
					v1 = j3d_vtx_val (data, size, &p, type, frac, &ok);
					bad |= !ok;
				}
				if (comps >= 3)
				{
					v2 = j3d_vtx_val (data, size, &p, type, frac, &ok);
					bad |= !ok;
				}
				if (bad)
					break;
				pool->v[i * 3] = v0;
				pool->v[i * 3 + 1] = v1;
				pool->v[i * 3 + 2] = v2;
			}
		}
		pool->n = count;
		if (d->num_nodes > 100000)
			break;
	}
	return d->pos.n > 0;
}

// --- EVP1 ---
static int j3d_parse_evp1 (const uint8_t *data, size_t size, size_t sect, j3d_dec_t *d)
{
	if (!j3d_ok (data, size, sect, 28))
		return 0;
	uint32_t ssize = j3d_rd32 (data + sect + 4);
	int n = j3d_rds16 (data + sect + 8);
	if (n < 0 || n > 100000)
		return 0;
	uint32_t o_cnt = j3d_rd32 (data + sect + 12), o_idx = j3d_rd32 (data + sect + 16),
			 o_w = j3d_rd32 (data + sect + 20), o_m = j3d_rd32 (data + sect + 24);
	d->num_evp = n;
	// NOTE: IBMs are read even when n==0 (our encoder always writes the
	// per-joint table; SuperBMD's empty 32-byte section carries none).
	int total = 0;
	if (n)
	{
		if (!j3d_ok (data, size, sect + o_cnt, (size_t)n) || !j3d_ok (data, size, sect, ssize))
			return 0;
		for (int i = 0; i < n; i++)
			total += data[sect + o_cnt + i];
		if (total < 0 || total > 1000000)
			return 0;
	}
	d->evp_wcount = CALLOC ((size_t)n ? (size_t)n : 1, sizeof (int));
	d->evp_bones = CALLOC ((size_t)total ? (size_t)total : 1, sizeof (uint16_t));
	d->evp_weights = CALLOC ((size_t)total ? (size_t)total : 1, sizeof (float));
	if (!d->evp_wcount || !d->evp_bones || !d->evp_weights)
		return 0;
	size_t pi = sect + o_idx, wi = sect + o_w;
	int t = 0;
	for (int i = 0; i < n; i++)
	{
		int c = data[sect + o_cnt + i];
		d->evp_wcount[i] = c;
		for (int k = 0; k < c; k++)
		{
			if (!j3d_ok (data, size, pi, 2))
				return 0;
			d->evp_bones[t++] = j3d_rd16 (data + pi);
			pi += 2;
		}
	}
	t = 0;
	for (int i = 0; i < n; i++)
		for (int k = 0; k < d->evp_wcount[i]; k++)
		{
			if (!j3d_ok (data, size, wi, 4))
				return 0;
			d->evp_weights[t++] = j3d_rdf32 (data + wi);
			wi += 4;
		}
	if (o_m && o_m < ssize)
	{
		int nm = (int)((ssize - o_m) / 48);
		if (nm < 0 || nm > 100000)
			return 0;
		d->evp_inv = CALLOC ((size_t)nm ? (size_t)nm * 12 : 12, sizeof (float));
		if (!d->evp_inv)
			return 0;
		d->num_inv = nm;
		for (int i = 0; i < nm; i++)
			for (int k = 0; k < 12; k++)
			{
				size_t o = sect + o_m + (size_t)i * 48 + (size_t)k * 4;
				if (!j3d_ok (data, size, o, 4))
					return 0;
				d->evp_inv[i * 12 + k] = j3d_rdf32 (data + o);
			}
	}
	return 1;
}

// --- DRW1 ---
static int j3d_parse_drw1 (const uint8_t *data, size_t size, size_t sect, j3d_dec_t *d)
{
	if (!j3d_ok (data, size, sect, 20))
		return 0;
	int n = j3d_rds16 (data + sect + 8);
	if (n < 0 || n > 100000)
		return 0;
	uint32_t o_b = j3d_rd32 (data + sect + 12), o_i = j3d_rd32 (data + sect + 16);
	d->num_drw = n;
	if (!n)
		return 1;
	if (!j3d_ok (data, size, sect + o_b, (size_t)n)
		|| !j3d_ok (data, size, sect + o_i, (size_t)n * 2))
		return 0;
	d->drw_weighted = MALLOC ((size_t)n);
	d->drw_idx = MALLOC ((size_t)n * 2);
	if (!d->drw_weighted || !d->drw_idx)
		return 0;
	for (int i = 0; i < n; i++)
	{
		d->drw_weighted[i] = data[sect + o_b + i] ? 1 : 0;
		d->drw_idx[i] = j3d_rd16 (data + sect + o_i + (size_t)i * 2);
	}
	return 1;
}

// --- JNT1 ---
static int j3d_parse_jnt1 (const uint8_t *data, size_t size, size_t sect, j3d_dec_t *d)
{
	if (!j3d_ok (data, size, sect, 24))
		return 0;
	int n = j3d_rds16 (data + sect + 8);
	if (n <= 0 || n > 100000)
		return 0;
	uint32_t o_j = j3d_rd32 (data + sect + 12), o_r = j3d_rd32 (data + sect + 16),
			 o_n = j3d_rd32 (data + sect + 20);
	char **names = 0;
	int nn = 0;
	if (!j3d_read_nametable (data, size, sect + o_n, &names, &nn))
		return 0;
	if (!j3d_ok (data, size, sect + o_r, (size_t)n * 2))
	{
		j3d_free_names (names, nn);
		return 0;
	}
	int highest = -1;
	int *remap = MALLOC ((size_t)n * sizeof (int));
	if (!remap)
	{
		j3d_free_names (names, nn);
		return 0;
	}
	for (int i = 0; i < n; i++)
	{
		remap[i] = j3d_rds16 (data + sect + o_r + (size_t)i * 2);
		if (remap[i] > highest)
			highest = remap[i];
	}
	if (highest < 0 || highest >= nn)
	{
		FREE (remap);
		j3d_free_names (names, nn);
		return 0;
	}
	d->num_joints_file = n;
	d->jnames = CALLOC ((size_t)n, sizeof (char *));
	d->jscale = CALLOC ((size_t)n * 3, sizeof (float));
	d->jeuler = CALLOC ((size_t)n * 3, sizeof (float));
	d->jtrans = CALLOC ((size_t)n * 3, sizeof (float));
	d->jparent = CALLOC ((size_t)n, sizeof (int));
	d->jmtxtype = CALLOC ((size_t)n, sizeof (int));
	if (!d->jnames || !d->jscale || !d->jeuler || !d->jtrans || !d->jparent || !d->jmtxtype)
	{
		FREE (remap);
		j3d_free_names (names, nn);
		return 0;
	}
	for (int i = 0; i < n; i++)
	{
		int src = remap[i];
		size_t jo = sect + o_j + (size_t)src * 64;
		if (!j3d_ok (data, size, jo, 64))
		{
			FREE (remap);
			j3d_free_names (names, nn);
			return 0;
		}
		d->jnames[i] = STRDUP (src < nn ? names[src] : "joint");
		d->jmtxtype[i] = j3d_rds16 (data + jo);
		for (int k = 0; k < 3; k++)
			d->jscale[i * 3 + k] = j3d_rdf32 (data + jo + 4 + (size_t)k * 4);
		for (int k = 0; k < 3; k++)
			d->jeuler[i * 3 + k]
				= (float)j3d_rds16 (data + jo + 16 + (size_t)k * 2) * 180.0f / 32768.0f;
		for (int k = 0; k < 3; k++)
			d->jtrans[i * 3 + k] = j3d_rdf32 (data + jo + 24 + (size_t)k * 4);
		d->jparent[i] = -2; // resolved from INF1 below
	}
	FREE (remap);
	j3d_free_names (names, nn);
	// hierarchy from INF1 joint nodes with open/close scope
	int *stack = MALLOC ((size_t)(d->num_nodes + 1) * sizeof (int));
	int sp = 0, cur = -1;
	if (stack)
	{
		for (int i = 0; i < d->num_nodes; i++)
		{
			int t = d->node_type[i], idx = d->node_idx[i];
			if (t == J3D_NODE_OPEN)
				stack[sp++] = cur;
			else if (t == J3D_NODE_CLOSE)
				cur = sp ? stack[--sp] : -1;
			else if (t == J3D_NODE_JOINT && idx >= 0 && idx < d->num_joints_file)
			{
				if (d->jparent[idx] == -2)
					d->jparent[idx] = cur;
			}
		}
		FREE (stack);
	}
	for (int i = 0; i < d->num_joints_file; i++)
		if (d->jparent[i] == -2)
			d->jparent[i] = -1;
	return 1;
}

// --- MAT3 (simplified: names, cull, diffuse, texture bindings) ---
// Full TEV preservation is out of scope for model_t; init records are
// parsed at their real 332-byte stride so texture indices stay exact.
static int j3d_parse_mat3 (const uint8_t *data, size_t size, size_t sect, j3d_dec_t *d)
{
	if (!j3d_ok (data, size, sect, 132))
		return 0;
	int is_bmd2 = !memcmp (data + 0, "J3D2bmd2", 8);
	uint32_t ssize = j3d_rd32 (data + sect + 4);
	int matcount = j3d_rds16 (data + sect + 8);
	if (matcount < 0 || matcount > 100000)
		return 0;
	uint32_t offs[30];
	for (int i = 0; i < 30; i++)
		offs[i] = j3d_rd32 (data + sect + 12 + (size_t)i * 4);
	// remap + names
	int *remap = 0;
	char **names = 0;
	int nn = 0;
	if (offs[1] && j3d_ok (data, size, sect + offs[1], (size_t)matcount * 2))
	{
		remap = MALLOC ((size_t)matcount * sizeof (int));
		for (int i = 0; i < matcount && remap; i++)
			remap[i] = j3d_rds16 (data + sect + offs[1] + (size_t)i * 2);
	}
	if (offs[2] && !j3d_read_nametable (data, size, sect + offs[2], &names, &nn))
		nn = 0;
	int highest = -1;
	for (int i = 0; remap && i < matcount; i++)
		if (remap[i] > highest)
			highest = remap[i];
	if (highest < 0)
		highest = matcount - 1;
	int nmats = matcount; // model_t uses entry order (post-remap copies)
	d->num_mats = nmats;
	d->mnames = CALLOC ((size_t)nmats ? (size_t)nmats : 1, sizeof (char *));
	d->mdiffuse = CALLOC ((size_t)nmats ? (size_t)nmats * 4 : 4, sizeof (float));
	d->mtex = CALLOC ((size_t)nmats ? (size_t)nmats : 1, sizeof (*d->mtex));
	d->mcull = CALLOC ((size_t)nmats ? (size_t)nmats : 1, 1);
	if (!d->mnames || !d->mdiffuse || !d->mtex || !d->mcull)
	{
		FREE (remap);
		j3d_free_names (names, nn);
		return 0;
	}
	for (int i = 0; i < nmats; i++)
	{
		for (int k = 0; k < 8; k++)
			d->mtex[i][k] = -1;
		d->mdiffuse[i * 4] = d->mdiffuse[i * 4 + 1] = d->mdiffuse[i * 4 + 2] = 1.0f;
		d->mdiffuse[i * 4 + 3] = 1.0f;
		d->mcull[i] = 2; // Back
		const char *nm = (remap && remap[i] >= 0 && remap[i] < nn)
			? names[remap[i]]
			: (i < nn ? names[i] : "material");
		d->mnames[i] = STRDUP (nm ? nm : "material");
	}
	// blocks needed: cull(4B s32), matcolor(4B RGBA), texremap(s16)
	const uint8_t *cullblk = offs[4] ? data + sect + offs[4] : 0;
	const uint8_t *mcolblk = offs[5] ? data + sect + offs[5] : 0;
	int16_t *texremap = 0;
	size_t ntexremap = 0;
	if (offs[15])
	{
		// span to next nonzero block
		uint32_t stop = ssize;
		for (int i = 16; i < 30; i++)
			if (offs[i] && offs[i] > offs[15])
			{
				stop = offs[i];
				break;
			}
		if (stop > offs[15])
		{
			ntexremap = (stop - offs[15]) / 2;
			texremap = MALLOC (ntexremap * 2);
			for (size_t i = 0; texremap && i < ntexremap; i++)
				texremap[i] = j3d_rds16 (data + sect + offs[15] + i * 2);
		}
	}
	if (offs[0] && remap)
	{
		// init records, 332 bytes each (bmd3); bmd2 lacks 6 blocks but the
		// record body we read (flag..texidx) is layout-identical.
		for (int i = 0; i < matcount; i++)
		{
			int ui = remap[i] < 0 ? 0 : remap[i];
			size_t rec = sect + offs[0] + (size_t)ui * 332;
			if (!j3d_ok (data, size, rec, 160))
				continue;
			uint8_t cullidx = data[rec + 1];
			if (cullblk
				&& j3d_ok (cullblk, size - (size_t)(cullblk - data), (size_t)cullidx * 4, 4))
			{
				int cm = (int)j3d_rd32 (cullblk + (size_t)cullidx * 4);
				if (cm >= 0 && cm <= 3)
					d->mcull[i] = (uint8_t)cm;
			}
			int16_t ci = j3d_rds16 (data + rec + 8);
			if (ci >= 0 && mcolblk
				&& j3d_ok (mcolblk, size - (size_t)(mcolblk - data), (size_t)ci * 4, 4))
			{
				d->mdiffuse[i * 4] = (float)mcolblk[(size_t)ci * 4] / 255;
				d->mdiffuse[i * 4 + 1] = (float)mcolblk[(size_t)ci * 4 + 1] / 255;
				d->mdiffuse[i * 4 + 2] = (float)mcolblk[(size_t)ci * 4 + 2] / 255;
				d->mdiffuse[i * 4 + 3] = (float)mcolblk[(size_t)ci * 4 + 3] / 255;
			}
			(void)is_bmd2;
			for (int k = 0; k < 8; k++)
			{
				int16_t ti = j3d_rds16 (data + rec + 132 + (size_t)k * 2);
				if (ti >= 0 && texremap && (size_t)ti < ntexremap && texremap[ti] >= 0)
					d->mtex[i][k] = texremap[ti];
			}
		}
	}
	FREE (texremap);
	FREE (remap);
	j3d_free_names (names, nn);
	return 1;
}

// --- TEX1 ---
static int j3d_parse_tex1 (const uint8_t *data, size_t size, size_t sect, j3d_dec_t *d)
{
	if (!j3d_ok (data, size, sect, 32))
		return 0;
	int n = j3d_rds16 (data + sect + 8);
	if (n < 0 || n > 4096)
		return 0;
	uint32_t o_hdr = j3d_rd32 (data + sect + 12), o_nam = j3d_rd32 (data + sect + 16);
	char **names = 0;
	int nn = 0;
	if (!j3d_read_nametable (data, size, sect + o_nam, &names, &nn))
		return 0;
	d->num_tex = n;
	d->tnames = CALLOC ((size_t)n ? (size_t)n : 1, sizeof (char *));
	d->tfmt = CALLOC ((size_t)n ? (size_t)n : 1, 1);
	d->twrap_s = CALLOC ((size_t)n ? (size_t)n : 1, 1);
	d->twrap_t = CALLOC ((size_t)n ? (size_t)n : 1, 1);
	d->tminf = CALLOC ((size_t)n ? (size_t)n : 1, 1);
	d->tmagf = CALLOC ((size_t)n ? (size_t)n : 1, 1);
	d->tw = CALLOC ((size_t)n ? (size_t)n : 1, sizeof (uint16_t));
	d->th = CALLOC ((size_t)n ? (size_t)n : 1, sizeof (uint16_t));
	d->tmips = CALLOC ((size_t)n ? (size_t)n : 1, 1);
	d->trgba = CALLOC ((size_t)n ? (size_t)n : 1, sizeof (uint8_t *));
	d->tmip_rgba = CALLOC ((size_t)n ? (size_t)n : 1, sizeof (uint8_t **));
	d->tmip_w = CALLOC ((size_t)n ? (size_t)n : 1, sizeof (uint16_t *));
	d->tmip_h = CALLOC ((size_t)n ? (size_t)n : 1, sizeof (uint16_t *));
	if (!d->tnames || !d->tfmt || !d->trgba)
	{
		j3d_free_names (names, nn);
		return 0;
	}
	for (int i = 0; i < n; i++)
	{
		size_t ho = sect + o_hdr + (size_t)i * 32;
		if (!j3d_ok (data, size, ho, 32))
			continue;
		d->tnames[i] = STRDUP (i < nn ? names[i] : "texture");
		d->tfmt[i] = data[ho];
		uint w = j3d_rd16 (data + ho + 2), h = j3d_rd16 (data + ho + 4);
		d->tw[i] = (uint16_t)w;
		d->th[i] = (uint16_t)h;
		d->twrap_s[i] = data[ho + 6];
		d->twrap_t[i] = data[ho + 7];
		int palon = data[ho + 8];
		int palfmt = data[ho + 9];
		int palcnt = j3d_rd16 (data + ho + 10);
		int paloff = (int)j3d_rd32 (data + ho + 12);
		d->tminf[i] = data[ho + 20];
		d->tmagf[i] = data[ho + 21];
		int imgcnt = data[ho + 24];
		int imgoff = (int)j3d_rd32 (data + ho + 28);
		const uint8_t *pal = 0;
		if (palon && palcnt > 0 && palcnt <= 16384)
		{
			size_t po = ho + (size_t)paloff;
			if (j3d_ok (data, size, po, (size_t)palcnt * 2))
				pal = data + po;
		}
		if (!w || !h || imgoff <= 0)
			continue;
		size_t pos = ho + (size_t)imgoff;
		d->trgba[i] = j3d_tex_decode (data, size, &pos, w, h, d->tfmt[i], pal, palfmt, palcnt);
		if (imgcnt > 1 && d->trgba[i])
		{
			int nm = imgcnt - 1;
			if (nm > 12)
				nm = 12;
			d->tmip_rgba[i] = CALLOC ((size_t)nm, sizeof (uint8_t *));
			d->tmip_w[i] = CALLOC ((size_t)nm, sizeof (uint16_t));
			d->tmip_h[i] = CALLOC ((size_t)nm, sizeof (uint16_t));
			int got = 0;
			for (int m = 0; m < nm; m++)
			{
				uint mw = w >> (m + 1), mh = h >> (m + 1);
				if (!mw)
					mw = 1;
				if (!mh)
					mh = 1;
				uint8_t *px
					= j3d_tex_decode (data, size, &pos, mw, mh, d->tfmt[i], pal, palfmt, palcnt);
				if (!px)
					break;
				d->tmip_rgba[i][got] = px;
				d->tmip_w[i][got] = (uint16_t)mw;
				d->tmip_h[i][got] = (uint16_t)mh;
				got++;
			}
			d->tmips[i] = (uint8_t)got;
		}
	}
	j3d_free_names (names, nn);
	return 1;
}

// --- 4x4 helpers (row-vector convention, matching OpenTK) ---
static void j3d_m4_from_34 (float m[16], const float *a)
{
	for (int i = 0; i < 12; i++)
		m[i] = a[i];
	m[12] = m[13] = m[14] = 0.0f;
	m[15] = 1.0f;
}
static int j3d_m4_invert (float out[16], const float m[16])
{
	// Gauss-Jordan on row-major 4x4
	float a[16];
	memcpy (a, m, sizeof (a));
	for (int i = 0; i < 16; i++)
		out[i] = (i % 5 == 0) ? 1.0f : 0.0f;
	for (int c = 0; c < 4; c++)
	{
		int piv = c;
		for (int r = c + 1; r < 4; r++)
			if (fabsf (a[r * 4 + c]) > fabsf (a[piv * 4 + c]))
				piv = r;
		if (fabsf (a[piv * 4 + c]) < 1e-12f)
			return 0;
		if (piv != c)
			for (int k = 0; k < 4; k++)
			{
				float t = a[c * 4 + k];
				a[c * 4 + k] = a[piv * 4 + k];
				a[piv * 4 + k] = t;
				t = out[c * 4 + k];
				out[c * 4 + k] = out[piv * 4 + k];
				out[piv * 4 + k] = t;
			}
		float d = a[c * 4 + c];
		for (int k = 0; k < 4; k++)
		{
			a[c * 4 + k] /= d;
			out[c * 4 + k] /= d;
		}
		for (int r = 0; r < 4; r++)
		{
			if (r == c)
				continue;
			float f = a[r * 4 + c];
			for (int k = 0; k < 4; k++)
			{
				a[r * 4 + k] -= f * a[c * 4 + k];
				out[r * 4 + k] -= f * out[c * 4 + k];
			}
		}
	}
	return 1;
}
static void j3d_xform_pt (const float m[16], const float in[3], float out[3])
{
	for (int j = 0; j < 3; j++)
		out[j] = in[0] * m[j] + in[1] * m[4 + j] + in[2] * m[8 + j] + m[12 + j];
}
static void j3d_xform_nrm33 (const float m[16], const float in[3], float out[3])
{
	for (int j = 0; j < 3; j++)
		out[j] = in[0] * m[j] + in[1] * m[4 + j] + in[2] * m[8 + j];
	float l = sqrtf (out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
	if (l > 1e-12f)
	{
		out[0] /= l;
		out[1] /= l;
		out[2] /= l;
	}
}

// --- SHP1 decode ---
typedef struct
{
	int has_pmtx, has_nrm, has_c[2], has_tex[8];
	int pmtx_direct; // 1 if Direct input
} j3d_desc_t;

typedef struct
{
	uint32_t pos, nrm, c[2], tex[8];
	uint32_t pmtx; // raw byte value (pre /3 for Direct)
	int drw; // resolved DRW1 slot
} j3d_vx_t;

typedef struct
{
	j3d_vx_t *v;
	int n, cap;
} j3d_tri_t;

static void j3d_tri_push (j3d_tri_t *t, const j3d_vx_t *v)
{
	if (t->n >= t->cap)
	{
		int nc = t->cap ? t->cap * 2 : 64;
		t->v = REALLOC (t->v, (size_t)nc * sizeof (*t->v));
		if (!t->v)
			return;
		t->cap = nc;
	}
	t->v[t->n++] = *v;
}
static int j3d_vx_samepos (const j3d_vx_t *a, const j3d_vx_t *b)
{
	return a->pos == b->pos;
}

// Parse one shape's packets into a triangle soup. Returns 1 on success.
static int j3d_expand_shape (const uint8_t *data, size_t size, size_t shp_sect, int shape_idx,
	int pkt_count, int attr_off_rel, int mtx_data_idx, int first_pkt, const int *pkt_sizes,
	const int *pkt_offs, int num_pkts, const int *mtx_cnts, const int *mtx_starts, int num_mtx,
	const uint8_t *mtx_idx_base, size_t mtx_idx_size, size_t prim_base, j3d_dec_t *d,
	j3d_tri_t *tris, int *has_pmtx_out, int *billboard)
{
	(void)shape_idx;
	size_t shp_size = j3d_rd32 (data + shp_sect + 4);
	(void)shp_size;
	// descriptor
	// descriptor (slot 3 of the 8 SHP1 offsets: shape, remap, unused, attr, ...)
	size_t attr_base = 0;
	{
		uint32_t o_desc = j3d_rd32 (data + shp_sect + 24);
		attr_base = shp_sect + o_desc;
	}
	j3d_desc_t desc;
	memset (&desc, 0, sizeof (desc));
	int dattrs[24], dtypes[24], nattrs = 0;
	{
		size_t dp = attr_base + (size_t)attr_off_rel;
		for (int i = 0; i < 24; i++)
		{
			if (!j3d_ok (data, size, dp, 8))
				return 0;
			int a = (int)j3d_rd32 (data + dp);
			int t = (int)j3d_rd32 (data + dp + 4);
			dp += 8;
			if (a == J3D_ATTR_NULL)
				break;
			dattrs[nattrs] = a;
			dtypes[nattrs] = t;
			nattrs++;
			if (a == J3D_ATTR_PMTX)
			{
				desc.has_pmtx = 1;
				desc.pmtx_direct = t == 1;
			}
			else if (a == J3D_ATTR_NRM)
				desc.has_nrm = 1;
			else if (a == J3D_ATTR_C0)
				desc.has_c[0] = 1;
			else if (a == J3D_ATTR_C1)
				desc.has_c[1] = 1;
			else if (a >= J3D_ATTR_T0 && a <= J3D_ATTR_T0 + 7)
				desc.has_tex[a - J3D_ATTR_T0] = 1;
		}
	}
	*has_pmtx_out = desc.has_pmtx;
	for (int p = 0; p < pkt_count; p++)
	{
		int pk = first_pkt + p;
		if (pk < 0 || pk >= num_pkts)
			continue;
		int mi = mtx_data_idx + p;
		int nm = (mi >= 0 && mi < num_mtx) ? mtx_cnts[mi] : 0;
		int ms = (mi >= 0 && mi < num_mtx) ? mtx_starts[mi] : 0;
		const uint8_t *mbase = mtx_idx_base + (size_t)ms * 2;
		size_t mleft = mtx_idx_size > (size_t)ms * 2 ? mtx_idx_size - (size_t)ms * 2 : 0;
		size_t pp = prim_base + (size_t)pkt_offs[pk];
		size_t pend = pp + (size_t)pkt_sizes[pk];
		// collect packet vertices in order with primitive boundaries
		typedef struct
		{
			j3d_vx_t *v;
			int n, cap, prim; // prim op of first vertex run
		} run_t;
		run_t *runs = 0;
		int nruns = 0, runs_cap = 0;
		while (pp < pend)
		{
			if (!j3d_ok (data, size, pp, 3))
				break;
			if (!data[pp])
				break;
			int op = data[pp] & 0xf8;
			int cnt = j3d_rd16 (data + pp + 1);
			pp += 3;
			if (cnt < 0 || cnt > 100000)
				break;
			if (nruns >= runs_cap)
			{
				int ncap = runs_cap ? runs_cap * 2 : 8;
				run_t *nr = REALLOC (runs, (size_t)ncap * sizeof (*nr));
				if (!nr)
				{
					for (int k = 0; k < nruns; k++)
						FREE (runs[k].v);
					FREE (runs);
					return 0;
				}
				runs = nr;
				runs_cap = ncap;
			}
			run_t *r = &runs[nruns++];
			r->v = 0;
			r->n = r->cap = 0;
			r->prim = op;
			for (int i = 0; i < cnt; i++)
			{
				j3d_vx_t vx;
				memset (&vx, 0xff, sizeof (vx)); // 0xffffffff = absent
				vx.drw = -1;
				vx.pmtx = 0;
				int bad = 0;
				for (int a = 0; a < nattrs; a++)
				{
					int t = dtypes[a];
					uint32_t idx = 0;
					if (t == 1 || t == 2) // Direct / Index8
					{
						if (!j3d_ok (data, size, pp, 1))
						{
							bad = 1;
							break;
						}
						idx = data[pp++];
					}
					else if (t == 3) // Index16
					{
						if (!j3d_ok (data, size, pp, 2))
						{
							bad = 1;
							break;
						}
						idx = j3d_rd16 (data + pp);
						pp += 2;
					}
					else
					{
						bad = 1;
						break;
					}
					int at = dattrs[a];
					if (at == J3D_ATTR_PMTX)
						vx.pmtx = idx;
					else if (at == J3D_ATTR_POS)
						vx.pos = idx;
					else if (at == J3D_ATTR_NRM)
						vx.nrm = idx;
					else if (at == J3D_ATTR_C0)
						vx.c[0] = idx;
					else if (at == J3D_ATTR_C1)
						vx.c[1] = idx;
					else if (at >= J3D_ATTR_T0 && at <= J3D_ATTR_T0 + 7)
						vx.tex[at - J3D_ATTR_T0] = idx;
				}
				if (bad)
					break;
				// resolve DRW slot (packet-local matrix index, like SuperBMD;
				// out-of-range indices fall back to the packet's first
				// matrix, mirroring the obj2bdl workaround in SHP1)
				int slot = -1;
				if (desc.has_pmtx)
				{
					int li = desc.pmtx_direct ? (int)(vx.pmtx / 3) : (int)vx.pmtx;
					if (li < 0 || li >= nm)
						li = 0;
					if (nm > 0 && (size_t)(li * 2 + 1) < mleft)
						slot = (int)j3d_rd16 (mbase + (size_t)li * 2);
					else if (nm > 0 && mleft >= 2)
						slot = (int)j3d_rd16 (mbase);
				}
				else if (nm > 0 && mleft >= 2)
					slot = (int)j3d_rd16 (mbase);
				vx.drw = slot;
				if (r->n >= r->cap)
				{
					int nc = r->cap ? r->cap * 2 : 32;
					r->v = REALLOC (r->v, (size_t)nc * sizeof (*r->v));
					if (!r->v)
					{
						r->cap = r->n = 0;
						break;
					}
					r->cap = nc;
				}
				r->v[r->n++] = vx;
			}
		}
		// expand runs to triangles
		for (int ri = 0; ri < nruns; ri++)
		{
			run_t *r = &runs[ri];
			if (r->prim == J3D_PRIM_TRI)
			{
				for (int i = 0; i + 2 < r->n; i += 3)
				{
					j3d_tri_push (tris, &r->v[i]);
					j3d_tri_push (tris, &r->v[i + 1]);
					j3d_tri_push (tris, &r->v[i + 2]);
				}
			}
			else if (r->prim == J3D_PRIM_STRIP)
			{
				for (int v = 2; v < r->n; v++)
				{
					int even = v % 2 != 0;
					j3d_vx_t t0 = r->v[v - 2], t1 = even ? r->v[v] : r->v[v - 1],
							 t2 = even ? r->v[v - 1] : r->v[v];
					if (!j3d_vx_samepos (&t0, &t1) && !j3d_vx_samepos (&t1, &t2)
						&& !j3d_vx_samepos (&t2, &t0))
					{
						j3d_tri_push (tris, &t0);
						j3d_tri_push (tris, &t1);
						j3d_tri_push (tris, &t2);
					}
				}
			}
			else if (r->prim == J3D_PRIM_FAN)
			{
				for (int v = 1; v + 1 < r->n; v++)
				{
					j3d_vx_t t0 = r->v[v], t1 = r->v[v + 1], t2 = r->v[0];
					if (!j3d_vx_samepos (&t0, &t1) && !j3d_vx_samepos (&t1, &t2)
						&& !j3d_vx_samepos (&t2, &t0))
					{
						j3d_tri_push (tris, &t0);
						j3d_tri_push (tris, &t1);
						j3d_tri_push (tris, &t2);
					}
				}
			}
			FREE (r->v);
		}
		FREE (runs);
	}
	(void)billboard;
	return 1;
}

// --- mesh builder (triangle soup -> deduped mesh_t) ---
typedef struct
{
	uint64_t *keys;
	int *vals;
	int cap, n;
} j3d_map_t;

static uint64_t j3d_hash_f3 (const float *p, int n, int seed)
{
	uint64_t h = (uint64_t)seed * 0x9e3779b97f4a7c15ULL + 0x100;
	for (int i = 0; i < n; i++)
	{
		uint32_t u;
		memcpy (&u, &p[i], 4);
		h ^= u + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
	}
	return h ? h : 1;
}
static int j3d_map_get (j3d_map_t *m, uint64_t key)
{
	if (!m->cap)
		return -1;
	int i = (int)(key & (uint64_t)(m->cap - 1));
	while (m->keys[i])
	{
		if (m->keys[i] == key)
			return m->vals[i];
		i = (i + 1) & (m->cap - 1);
	}
	return -1;
}
static void j3d_map_put (j3d_map_t *m, uint64_t key, int val)
{
	if (m->n * 2 >= m->cap)
	{
		int nc = m->cap ? m->cap * 2 : 256;
		uint64_t *ok = m->keys;
		int *ov = m->vals, oc = m->cap;
		m->keys = CALLOC ((size_t)nc, sizeof (uint64_t));
		m->vals = MALLOC ((size_t)nc * sizeof (int));
		m->cap = nc;
		m->n = 0;
		if (!m->keys || !m->vals)
		{
			FREE (m->keys);
			FREE (m->vals);
			m->keys = ok;
			m->vals = ov;
			m->cap = oc;
			// recount n
			m->n = 0;
			for (int i = 0; i < oc; i++)
				if (ok[i])
					m->n++;
			return;
		}
		for (int i = 0; i < oc; i++)
			if (ok[i])
				j3d_map_put (m, ok[i], ov[i]);
		FREE (ok);
		FREE (ov);
	}
	int i = (int)(key & (uint64_t)(m->cap - 1));
	while (m->keys[i])
		i = (i + 1) & (m->cap - 1);
	m->keys[i] = key;
	m->vals[i] = val;
	m->n++;
}

// Compose T* Rz*Ry*Rx *S like dae_joint_trs (degrees in).
static void j3d_joint_trs (float out[12], float sx, float sy, float sz, float rx, float ry,
	float rz, float tx, float ty, float tz)
{
	double dx = rx * (M_PI / 180.0), dy = ry * (M_PI / 180.0), dz = rz * (M_PI / 180.0);
	float cx = (float)cos (dx), snx = (float)sin (dx), cy = (float)cos (dy), sny = (float)sin (dy),
		  cz = (float)cos (dz), snz = (float)sin (dz);
	float rot[12] = { cz * cy, cz * sny * snx - snz * cx, cz * sny * cx + snz * snx, 0.0f, snz * cy,
		snz * sny * snx + cz * cx, snz * sny * cx - cz * snx, 0.0f, -sny, cy * snx, cy * cx, 0.0f };
	for (unsigned r = 0; r < 3; r++)
	{
		out[r * 4] = rot[r * 4] * sx;
		out[r * 4 + 1] = rot[r * 4 + 1] * sy;
		out[r * 4 + 2] = rot[r * 4 + 2] * sz;
	}
	out[3] = tx;
	out[7] = ty;
	out[11] = tz;
}

static int j3d_build_mesh (mesh_t *mesh, j3d_tri_t *tris, j3d_dec_t *d, int has_pmtx)
{
	memset (mesh, 0, sizeof (*mesh));
	if (!tris->n)
		return 0;
	// resolve per-corner attributes
	typedef struct
	{
		float p[3], n[3], uv[8][2];
		float c[2][4];
		int hasn, hasc[2], hast[8];
		int drw;
	} corner_t;
	corner_t *corners = MALLOC ((size_t)tris->n * sizeof (*corners));
	if (!corners)
		return 0;
	float ibm[16], ibm_inv[16], ibm33inv[16];
	for (int i = 0; i < tris->n; i++)
	{
		j3d_vx_t *vx = &tris->v[i];
		corner_t *c = &corners[i];
		memset (c, 0, sizeof (*c));
		uint32_t pi = vx->pos < (uint32_t)d->pos.n ? vx->pos : 0;
		c->p[0] = d->pos.v[pi * 3];
		c->p[1] = d->pos.v[pi * 3 + 1];
		c->p[2] = d->pos.v[pi * 3 + 2];
		c->hasn = 0;
		if (vx->nrm != 0xffffffffu && d->has_nrm && vx->nrm < (uint32_t)d->nrm.n)
		{
			c->n[0] = d->nrm.v[vx->nrm * 3];
			c->n[1] = d->nrm.v[vx->nrm * 3 + 1];
			c->n[2] = d->nrm.v[vx->nrm * 3 + 2];
			c->hasn = 1;
		}
		for (int t = 0; t < 2; t++)
		{
			c->hasc[t] = 0;
			if (vx->c[t] != 0xffffffffu && d->has_c[t])
			{
				j3d_pool_t *pool = t ? &d->c1 : &d->c0;
				if (vx->c[t] < (uint32_t)pool->n)
				{
					memcpy (c->c[t], pool->v + (size_t)vx->c[t] * 4, 4 * sizeof (float));
					c->hasc[t] = 1;
				}
			}
		}
		for (int t = 0; t < 8; t++)
		{
			c->hast[t] = 0;
			if (vx->tex[t] != 0xffffffffu && d->has_tex[t] && vx->tex[t] < (uint32_t)d->tex[t].n)
			{
				c->uv[t][0] = d->tex[t].v[(size_t)vx->tex[t] * 3];
				c->uv[t][1] = d->tex[t].v[(size_t)vx->tex[t] * 3 + 1];
				c->hast[t] = 1;
			}
		}
		int slot = vx->drw;
		if (slot < 0 || slot >= d->num_drw)
			slot = d->num_drw ? 0 : -1;
		c->drw = slot;
		// single-weight verts are stored bone-local: untransform by IBM^-1
		int single = 0, bone = 0;
		if (slot >= 0 && !d->drw_weighted[slot])
		{
			single = 1;
			bone = d->drw_idx[slot];
		}
		if (single && bone >= 0 && bone < d->num_joints_file && bone < d->num_inv)
		{
			j3d_m4_from_34 (ibm, d->evp_inv + (size_t)bone * 12);
			if (j3d_m4_invert (ibm_inv, ibm))
			{
				float p[3];
				memcpy (p, c->p, sizeof (p));
				j3d_xform_pt (ibm_inv, p, c->p);
				if (c->hasn)
				{
					// worldN = storedN x IBM33  =>  storedN = worldN x IBM33^-1
					float m3[16];
					memcpy (m3, ibm, sizeof (m3));
					m3[3] = m3[7] = m3[11] = m3[12] = m3[13] = m3[14] = 0.0f;
					m3[15] = 1.0f;
					if (j3d_m4_invert (ibm33inv, m3))
					{
						float n[3];
						memcpy (n, c->n, sizeof (n));
						j3d_xform_nrm33 (ibm33inv, n, c->n);
					}
				}
			}
		}
	}
	(void)has_pmtx;
	// dedup into mesh pools
	size_t cap = 64;
	mesh->positions = MALLOC (cap * sizeof (vec3_t));
	mesh->normals = MALLOC (cap * sizeof (vec3_t));
	mesh->texcoords = MALLOC (cap * sizeof (vec2_t));
	mesh->colors[0] = MALLOC (cap * sizeof (color4_t));
	mesh->colors[1] = MALLOC (cap * sizeof (color4_t));
	mesh->position_node = MALLOC (cap * sizeof (int));
	mesh->vertices = MALLOC ((size_t)tris->n * sizeof (vertex_t));
	vec2_t *ex[7] = { 0 };
	for (int t = 0; t < 7; t++)
		ex[t] = MALLOC (cap * sizeof (vec2_t));
	j3d_map_t map;
	memset (&map, 0, sizeof (map));
	if (!mesh->positions || !mesh->normals || !mesh->texcoords || !mesh->vertices
		|| !mesh->position_node)
	{
		FREE (corners);
		return 0;
	}
	mesh->num_vertices = (size_t)tris->n;
	for (int i = 0; i < tris->n; i++)
	{
		corner_t *c = &corners[i];
		uint64_t h = j3d_hash_f3 (c->p, 3, 11);
		h ^= j3d_hash_f3 (c->n, 3, 12) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
		h ^= j3d_hash_f3 (c->uv[0], 2, 13) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
		h ^= j3d_hash_f3 (c->c[0], 4, 14) + (uint64_t)(c->drw + 1);
		int slot = j3d_map_get (&map, h);
		if (slot < 0)
		{
			slot = (int)mesh->num_positions;
			if (mesh->num_positions >= cap)
			{
				cap *= 2;
				mesh->positions = REALLOC (mesh->positions, cap * sizeof (vec3_t));
				mesh->normals = REALLOC (mesh->normals, cap * sizeof (vec3_t));
				mesh->texcoords = REALLOC (mesh->texcoords, cap * sizeof (vec2_t));
				mesh->colors[0] = REALLOC (mesh->colors[0], cap * sizeof (color4_t));
				mesh->colors[1] = REALLOC (mesh->colors[1], cap * sizeof (color4_t));
				mesh->position_node = REALLOC (mesh->position_node, cap * sizeof (int));
				for (int t = 0; t < 7; t++)
					ex[t] = REALLOC (ex[t], cap * sizeof (vec2_t));
				if (!mesh->positions)
				{
					FREE (corners);
					return 0;
				}
			}
			mesh->positions[slot].x = c->p[0];
			mesh->positions[slot].y = c->p[1];
			mesh->positions[slot].z = c->p[2];
			mesh->normals[slot].x = c->n[0];
			mesh->normals[slot].y = c->n[1];
			mesh->normals[slot].z = c->n[2];
			mesh->texcoords[slot].u = c->uv[0][0];
			mesh->texcoords[slot].v = c->uv[0][1];
			for (int t = 0; t < 7; t++)
			{
				ex[t][slot].u = c->uv[t + 1][0];
				ex[t][slot].v = c->uv[t + 1][1];
			}
			for (int t = 0; t < 2; t++)
			{
				mesh->colors[t][slot].r = c->c[t][0];
				mesh->colors[t][slot].g = c->c[t][1];
				mesh->colors[t][slot].b = c->c[t][2];
				mesh->colors[t][slot].a = c->c[t][3];
			}
			mesh->position_node[slot] = c->drw;
			mesh->num_positions++;
			j3d_map_put (&map, h, slot);
		}
		vertex_t *v = &mesh->vertices[i];
		v->position_idx = slot;
		v->normal_idx = slot;
		v->tangent_idx = -1;
		v->texcoord_idx = slot;
		v->matrix_idx = -1;
		v->color_idx[0] = v->color_idx[1] = slot;
		for (int t = 0; t < 7; t++)
			v->extra_texcoord_idx[t] = slot;
	}
	FREE (corners);
	FREE (map.keys);
	FREE (map.vals);
	mesh->num_normals = mesh->num_positions;
	mesh->num_texcoords = mesh->num_positions;
	// colors / extra uvs only kept when some corner actually had them
	int anyc[2] = { 0, 0 }, anyt[8] = { 0 };
	for (int i = 0; i < tris->n; i++)
	{
		j3d_vx_t *vx = &tris->v[i];
		if (vx->c[0] != 0xffffffffu && d->has_c[0])
			anyc[0] = 1;
		if (vx->c[1] != 0xffffffffu && d->has_c[1])
			anyc[1] = 1;
		for (int t = 0; t < 8; t++)
			if (vx->tex[t] != 0xffffffffu && d->has_tex[t])
				anyt[t] = 1;
	}
	for (int t = 0; t < 2; t++)
		if (anyc[t])
			mesh->num_colors[t] = mesh->num_positions;
		else
		{
			FREE (mesh->colors[t]);
			mesh->colors[t] = 0;
			for (size_t i = 0; i < mesh->num_vertices; i++)
				mesh->vertices[i].color_idx[t] = -1;
		}
	if (!anyt[0])
	{
		FREE (mesh->texcoords);
		mesh->texcoords = 0;
		mesh->num_texcoords = 0;
		for (size_t i = 0; i < mesh->num_vertices; i++)
			mesh->vertices[i].texcoord_idx = -1;
	}
	mesh->extra_texcoords[0] = 0;
	for (int t = 0; t < 7; t++)
	{
		if (anyt[t + 1])
		{
			mesh->extra_texcoords[t] = ex[t];
			mesh->num_extra_texcoords[t] = mesh->num_positions;
		}
		else
		{
			FREE (ex[t]);
			for (size_t i = 0; i < mesh->num_vertices; i++)
				mesh->vertices[i].extra_texcoord_idx[t] = -1;
		}
	}
	if (!d->has_nrm)
	{
		FREE (mesh->normals);
		mesh->normals = 0;
		mesh->num_normals = 0;
		for (size_t i = 0; i < mesh->num_vertices; i++)
			mesh->vertices[i].normal_idx = -1;
	}
	return 1;
}

model_t *ParseJ3D (const uint8_t *data, size_t size)
{
	size_t o_inf1, o_vtx1, o_evp1, o_drw1, o_jnt1, o_shp1, o_mat3, o_mdl3, o_tex1;
	int is_bdl = 0;
	if (!j3d_find_sections (data, size, &o_inf1, &o_vtx1, &o_evp1, &o_drw1, &o_jnt1, &o_shp1,
			&o_mat3, &o_mdl3, &o_tex1, &is_bdl))
		return 0;
	(void)o_mdl3;
	j3d_dec_t d;
	memset (&d, 0, sizeof (d));
	model_t *model = CALLOC (1, sizeof (*model));
	if (!model)
		return 0;
	if (!j3d_parse_inf1 (data, size, o_inf1, &d) || !j3d_parse_vtx1 (data, size, o_vtx1, &d)
		|| (o_evp1 && !j3d_parse_evp1 (data, size, o_evp1, &d))
		|| (o_drw1 && !j3d_parse_drw1 (data, size, o_drw1, &d))
		|| !j3d_parse_jnt1 (data, size, o_jnt1, &d) || !j3d_parse_mat3 (data, size, o_mat3, &d)
		|| !j3d_parse_tex1 (data, size, o_tex1, &d))
	{
		j3d_dec_free (&d);
		FreeModel (model);
		return 0;
	}
	// joints
	model->num_joints = (size_t)d.num_joints_file;
	model->joints = CALLOC (model->num_joints ? model->num_joints : 1, sizeof (joint_t));
	if (!model->joints)
	{
		j3d_dec_free (&d);
		FreeModel (model);
		return 0;
	}
	for (int i = 0; i < d.num_joints_file; i++)
	{
		joint_t *j = &model->joints[i];
		snprintf (j->name, sizeof (j->name), "%s", d.jnames[i] ? d.jnames[i] : "joint");
		j->parent_idx = d.jparent[i];
		j->translate.x = d.jtrans[i * 3];
		j->translate.y = d.jtrans[i * 3 + 1];
		j->translate.z = d.jtrans[i * 3 + 2];
		j->rotate.x = d.jeuler[i * 3];
		j->rotate.y = d.jeuler[i * 3 + 1];
		j->rotate.z = d.jeuler[i * 3 + 2];
		j->scale.x = d.jscale[i * 3];
		j->scale.y = d.jscale[i * 3 + 1];
		j->scale.z = d.jscale[i * 3 + 2];
		j3d_joint_trs (j->bind, j->scale.x, j->scale.y, j->scale.z, j->rotate.x, j->rotate.y,
			j->rotate.z, j->translate.x, j->translate.y, j->translate.z);
		if (i < d.num_inv)
			memcpy (j->inverse_bind, d.evp_inv + (size_t)i * 12, 12 * sizeof (float));
		else
		{
			memset (j->inverse_bind, 0, sizeof (j->inverse_bind));
			j->inverse_bind[0] = j->inverse_bind[5] = j->inverse_bind[10] = 1.0f;
		}
		j->has_inverse_bind = 1;
	}
	// skin influences: DRW1 slot -> weight list
	model->num_node_influences = (size_t)(d.num_drw > 0 ? d.num_drw : 0);
	if (model->num_node_influences)
	{
		model->node_influences = CALLOC (model->num_node_influences, sizeof (node_influence_t));
		if (!model->node_influences)
		{
			j3d_dec_free (&d);
			FreeModel (model);
			return 0;
		}
		for (int i = 0; i < d.num_drw; i++)
		{
			node_influence_t *ni = &model->node_influences[i];
			if (!d.drw_weighted[i])
			{
				ni->weights = MALLOC (sizeof (influence_t));
				if (ni->weights)
				{
					ni->weights[0].bone_idx = d.drw_idx[i] < d.num_joints_file ? d.drw_idx[i] : 0;
					ni->weights[0].weight = 1.0f;
					ni->num_weights = 1;
				}
			}
			else
			{
				int e = d.drw_idx[i];
				if (e >= 0 && e < d.num_evp)
				{
					int c = d.evp_wcount[e];
					int off = 0;
					for (int k = 0; k < e; k++)
						off += d.evp_wcount[k];
					ni->weights = MALLOC ((size_t)(c ? c : 1) * sizeof (influence_t));
					if (ni->weights)
					{
						ni->num_weights = 0;
						for (int k = 0; k < c; k++)
						{
							int b = d.evp_bones[off + k];
							if (b < 0 || b >= d.num_joints_file)
								continue;
							ni->weights[ni->num_weights].bone_idx = b;
							ni->weights[ni->num_weights].weight = d.evp_weights[off + k];
							ni->num_weights++;
						}
					}
				}
			}
		}
	}
	// materials (default fallback when MAT3 is empty)
	model->num_materials = (size_t)(d.num_mats > 0 ? d.num_mats : 0);
	if (!model->num_materials)
	{
		model->num_materials = 1;
		model->materials = CALLOC (1, sizeof (material_t));
		if (!model->materials)
		{
			j3d_dec_free (&d);
			FreeModel (model);
			return 0;
		}
		snprintf (model->materials[0].name, 64, "material");
		model->materials[0].diffuse[0] = model->materials[0].diffuse[1]
			= model->materials[0].diffuse[2] = 0.8f;
		model->materials[0].diffuse[3] = 1.0f;
	}
	else
	{
		model->materials = CALLOC (model->num_materials, sizeof (material_t));
		if (!model->materials)
		{
			j3d_dec_free (&d);
			FreeModel (model);
			return 0;
		}
		for (int i = 0; i < d.num_mats; i++)
		{
			material_t *m = &model->materials[i];
			snprintf (m->name, sizeof (m->name), "%s", d.mnames[i] ? d.mnames[i] : "material");
			m->diffuse[0] = d.mdiffuse[i * 4];
			m->diffuse[1] = d.mdiffuse[i * 4 + 1];
			m->diffuse[2] = d.mdiffuse[i * 4 + 2];
			m->diffuse[3] = d.mdiffuse[i * 4 + 3];
			m->num_textures = 0;
			m->has_alpha = 0;
			for (int k = 0; k < 8 && m->num_textures < 8; k++)
			{
				int ti = d.mtex[i][k];
				if (ti < 0 || ti >= d.num_tex)
					continue;
				snprintf (m->textures[m->num_textures], 64, "%s",
					d.tnames[ti] ? d.tnames[ti] : "texture");
				int w = d.twrap_s[ti] <= 2 ? d.twrap_s[ti] : 0;
				int wt = d.twrap_t[ti] <= 2 ? d.twrap_t[ti] : 0;
				m->wrap_s[m->num_textures] = (uint8_t)w;
				m->wrap_t[m->num_textures] = (uint8_t)wt;
				m->min_filter[m->num_textures] = 1;
				m->mag_filter[m->num_textures] = 1;
				m->texture_coord[m->num_textures] = m->num_textures;
				int f = d.tfmt[ti];
				if (f == J3D_TEX_IA4 || f == J3D_TEX_IA8 || f == J3D_TEX_RGB5A3
					|| f == J3D_TEX_RGBA32 || f == J3D_TEX_C4 || f == J3D_TEX_C8
					|| f == J3D_TEX_CMPR)
					m->has_alpha = 1;
				m->num_textures++;
			}
		}
	}
	// SHP1 tables
	uint32_t shp_size = j3d_rd32 (data + o_shp1 + 4);
	int entry_count = j3d_rds16 (data + o_shp1 + 8);
	uint32_t o_shape = j3d_rd32 (data + o_shp1 + 12), o_remap = j3d_rd32 (data + o_shp1 + 16),
			 /* o_attr = j3d_rd32 (data + o_shp1 + 24), */ o_midx = j3d_rd32 (data + o_shp1 + 28),
			 o_prim = j3d_rd32 (data + o_shp1 + 32), o_mdat = j3d_rd32 (data + o_shp1 + 36),
			 o_pinf = j3d_rd32 (data + o_shp1 + 40);
	if (entry_count < 0 || entry_count > 100000 || !j3d_ok (data, size, o_shp1 + o_remap, 2))
	{
		j3d_dec_free (&d);
		FreeModel (model);
		return 0;
	}
	int *remap = MALLOC ((size_t)entry_count * sizeof (int));
	int highest = -1;
	for (int i = 0; remap && i < entry_count; i++)
	{
		remap[i] = j3d_rds16 (data + o_shp1 + o_remap + (size_t)i * 2);
		if (remap[i] > highest)
			highest = remap[i];
	}
	int num_pkts = o_pinf < shp_size ? (int)((shp_size - o_pinf) / 8) : 0;
	int num_mtx = (o_pinf > o_mdat) ? (int)((o_pinf - o_mdat) / 8) : 0;
	int *pkt_sizes = 0, *pkt_offs = 0, *mtx_cnts = 0, *mtx_starts = 0;
	if (num_pkts > 0)
	{
		pkt_sizes = MALLOC ((size_t)num_pkts * sizeof (int));
		pkt_offs = MALLOC ((size_t)num_pkts * sizeof (int));
		for (int i = 0; pkt_sizes && i < num_pkts; i++)
		{
			pkt_sizes[i] = (int)j3d_rd32 (data + o_shp1 + o_pinf + (size_t)i * 8);
			pkt_offs[i] = (int)j3d_rd32 (data + o_shp1 + o_pinf + (size_t)i * 8 + 4);
		}
	}
	if (num_mtx > 0)
	{
		mtx_cnts = MALLOC ((size_t)num_mtx * sizeof (int));
		mtx_starts = MALLOC ((size_t)num_mtx * sizeof (int));
		for (int i = 0; mtx_cnts && i < num_mtx; i++)
		{
			mtx_cnts[i] = j3d_rds16 (data + o_shp1 + o_mdat + (size_t)i * 8 + 2);
			mtx_starts[i] = (int)j3d_rd32 (data + o_shp1 + o_mdat + (size_t)i * 8 + 4);
		}
	}
	// INF1 shape -> material map (entry order)
	int *shape_mat = MALLOC ((size_t)(entry_count ? entry_count : 1) * sizeof (int));
	if (shape_mat)
		for (int i = 0; i < entry_count; i++)
			shape_mat[i] = -1;
	if (shape_mat)
	{
		int *mstack = MALLOC ((size_t)(d.num_nodes + 1) * sizeof (int));
		int sp = 0, cur = -1;
		if (mstack)
		{
			for (int i = 0; i < d.num_nodes; i++)
			{
				int t = d.node_type[i], idx = d.node_idx[i];
				if (t == J3D_NODE_OPEN)
					mstack[sp++] = cur;
				else if (t == J3D_NODE_CLOSE)
					cur = sp ? mstack[--sp] : -1;
				else if (t == J3D_NODE_MAT)
					cur = idx;
				else if (t == J3D_NODE_SHAPE && idx >= 0 && idx < entry_count && shape_mat[idx] < 0)
					shape_mat[idx] = cur;
			}
			FREE (mstack);
		}
	}
	// expand entries into meshes
	model->meshes = 0;
	model->num_meshes = 0;
	if (entry_count > 0)
	{
		model->meshes = CALLOC ((size_t)entry_count, sizeof (mesh_t));
		if (!model->meshes)
		{
			FREE (remap);
			FREE (pkt_sizes);
			FREE (pkt_offs);
			FREE (mtx_cnts);
			FREE (mtx_starts);
			FREE (shape_mat);
			j3d_dec_free (&d);
			FreeModel (model);
			return 0;
		}
		for (int e = 0; e < entry_count; e++)
		{
			mesh_t *mesh = &model->meshes[model->num_meshes];
			int s = remap ? remap[e] : e;
			if (s < 0 || s > highest)
				continue;
			size_t ho = o_shp1 + o_shape + (size_t)s * 40;
			if (!j3d_ok (data, size, ho, 40))
				continue;
			int mtxtype = data[ho];
			int pkt_count = j3d_rds16 (data + ho + 2);
			int attr_rel = j3d_rds16 (data + ho + 4);
			int mtx_idx = j3d_rds16 (data + ho + 6);
			int first_pkt = j3d_rds16 (data + ho + 8);
			if (pkt_count < 0 || pkt_count > 100000)
				continue;
			j3d_tri_t tris;
			memset (&tris, 0, sizeof (tris));
			int has_pmtx = 0;
			size_t prim_base = o_shp1 + o_prim;
			size_t midx_size = 0;
			if (o_mdat > o_midx)
				midx_size = o_mdat - o_midx;
			else if (shp_size > o_midx)
				midx_size = shp_size - o_midx;
			if (!j3d_expand_shape (data, size, o_shp1, s, pkt_count, attr_rel, mtx_idx, first_pkt,
					pkt_sizes, pkt_offs, num_pkts, mtx_cnts, mtx_starts, num_mtx,
					data + o_shp1 + o_midx, midx_size, prim_base, &d, &tris, &has_pmtx, 0))
			{
				FREE (tris.v);
				continue;
			}
			if (!tris.n)
			{
				FREE (tris.v);
				continue;
			}
			if (!j3d_build_mesh (mesh, &tris, &d, has_pmtx))
			{
				FREE (tris.v);
				continue;
			}
			FREE (tris.v);
			if (mtxtype == 1)
				snprintf (mesh->name, sizeof (mesh->name), "mesh_%d_BillXY", e);
			else if (mtxtype == 2)
				snprintf (mesh->name, sizeof (mesh->name), "mesh_%d_BillX", e);
			else
				snprintf (mesh->name, sizeof (mesh->name), "mesh_%d", e);
			int mi = (shape_mat && shape_mat[e] >= 0) ? shape_mat[e] : 0;
			if (mi < 0 || (size_t)mi >= model->num_materials)
				mi = model->num_materials ? 0 : -1;
			mesh->material_idx = mi;
			model->num_meshes++;
		}
	}
	FREE (remap);
	FREE (pkt_sizes);
	FREE (pkt_offs);
	FREE (mtx_cnts);
	FREE (mtx_starts);
	FREE (shape_mat);
	j3d_dec_free (&d);
	if (!model->num_meshes)
	{
		FreeModel (model);
		return 0;
	}
	return model;
}

// --- texture staging + sidecars + profile ---
static void j3d_json_escape (FILE *f, const char *s)
{
	for (; s && *s; s++)
	{
		unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\')
			fprintf (f, "\\%c", c);
		else if (c < 0x20)
			fprintf (f, "\\u%04x", c);
		else
			fputc (c, f);
	}
}

static int j3d_save_png (const uint8_t *rgba, uint w, uint h, const char *path)
{
	if (!rgba || !w || !h)
		return 0;
	uint8_t *copy = MALLOC ((size_t)w * h * 4);
	if (!copy)
		return 0;
	memcpy (copy, rgba, (size_t)w * h * 4);
	Image_t img2;
	memset (&img2, 0, sizeof (img2));
	AssignDecodedRGBA (&img2, copy, w, h, &be_func, path);
	enumError err = SaveIMG (&img2, FF_PNG, 0, 0, path, true);
	ResetIMG (&img2);
	return err == ERR_OK;
}

int ExportJ3DTexturesFromData (const uint8_t *data, size_t size, const char *dest_glb)
{
	size_t o_inf1, o_vtx1, o_evp1, o_drw1, o_jnt1, o_shp1, o_mat3, o_mdl3, o_tex1;
	if (!j3d_find_sections (data, size, &o_inf1, &o_vtx1, &o_evp1, &o_drw1, &o_jnt1, &o_shp1,
			&o_mat3, &o_mdl3, &o_tex1, 0))
		return 0;
	j3d_dec_t d;
	memset (&d, 0, sizeof (d));
	if (!j3d_parse_tex1 (data, size, o_tex1, &d))
		return 0;
	char dir[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", dest_glb);
	char *slash = strrchr (dir, '/');
	if (slash)
		*slash = 0;
	else
		snprintf (dir, sizeof (dir), ".");
	int staged = 0;
	for (int i = 0; i < d.num_tex; i++)
	{
		if (!d.trgba[i])
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s.png", dir, d.tnames[i] ? d.tnames[i] : "texture");
		struct stat st;
		if (!stat (path, &st))
			continue; // never clobber user files
		if (j3d_save_png (d.trgba[i], d.tw[i], d.th[i], path))
			staged++;
		for (int m = 0; m < d.tmips[i]; m++)
		{
			char mpath[PATH_MAX];
			snprintf (mpath, sizeof (mpath), "%s/%s_mip%d.png", dir,
				d.tnames[i] ? d.tnames[i] : "texture", m + 1);
			if (!stat (mpath, &st))
				continue;
			j3d_save_png (d.tmip_rgba[i][m], d.tmip_w[i][m], d.tmip_h[i][m], mpath);
		}
	}
	j3d_dec_free (&d);
	return staged;
}

static const char *j3d_fmt_name (int f)
{
	switch (f)
	{
		case J3D_TEX_I4:
			return "I4";
		case J3D_TEX_I8:
			return "I8";
		case J3D_TEX_IA4:
			return "IA4";
		case J3D_TEX_IA8:
			return "IA8";
		case J3D_TEX_RGB565:
			return "RGB565";
		case J3D_TEX_RGB5A3:
			return "RGB5A3";
		case J3D_TEX_RGBA32:
			return "RGBA32";
		case J3D_TEX_C4:
			return "C4";
		case J3D_TEX_C8:
			return "C8";
		case J3D_TEX_C14X2:
			return "C14X2";
		case J3D_TEX_CMPR:
			return "CMPR";
	}
	return "UNKNOWN";
}
static const char *j3d_wrap_name (int w)
{
	return w == 1 ? "Repeat" : w == 2 ? "MirroredRepeat" : "ClampToEdge";
}
static const char *j3d_filter_name (int f)
{
	switch (f)
	{
		case 0:
			return "Nearest";
		case 1:
			return "Linear";
		case 2:
			return "NearestMipmapNearest";
		case 3:
			return "NearestMipmapLinear";
		case 4:
			return "LinearMipmapNearest";
		case 5:
			return "LinearMipmapLinear";
	}
	return "Linear";
}

int ExportJ3DMaterialsJSON (const uint8_t *data, size_t size, const char *json_path)
{
	size_t o_inf1, o_vtx1, o_evp1, o_drw1, o_jnt1, o_shp1, o_mat3, o_mdl3, o_tex1;
	if (!j3d_find_sections (data, size, &o_inf1, &o_vtx1, &o_evp1, &o_drw1, &o_jnt1, &o_shp1,
			&o_mat3, &o_mdl3, &o_tex1, 0))
		return 0;
	j3d_dec_t d;
	memset (&d, 0, sizeof (d));
	// need MAT3 + TEX1 only, but parsers are section-local
	if (!j3d_parse_mat3 (data, size, o_mat3, &d) || !j3d_parse_tex1 (data, size, o_tex1, &d))
	{
		j3d_dec_free (&d);
		return 0;
	}
	FILE *f = fopen (json_path, "w");
	if (!f)
	{
		j3d_dec_free (&d);
		return 0;
	}
	fprintf (f, "{\n  \"generator\": \"nintoolbox-j3d\",\n  \"materials\": [\n");
	for (int i = 0; i < d.num_mats; i++)
	{
		fprintf (f, "    {\"name\": \"");
		j3d_json_escape (f, d.mnames[i]);
		fprintf (f, "\", \"cull\": \"%s\", \"diffuse\": [%g, %g, %g, %g], \"textures\": [",
			d.mcull[i] == 0		  ? "None"
				: d.mcull[i] == 1 ? "Front"
				: d.mcull[i] == 3 ? "All"
								  : "Back",
			d.mdiffuse[i * 4], d.mdiffuse[i * 4 + 1], d.mdiffuse[i * 4 + 2], d.mdiffuse[i * 4 + 3]);
		for (int k = 0; k < 8; k++)
		{
			int ti = d.mtex[i][k];
			if (ti >= 0 && ti < d.num_tex)
			{
				fprintf (f, "%s\"", k ? ", " : "");
				j3d_json_escape (f, d.tnames[ti]);
				fputc ('"', f);
			}
			else
				fprintf (f, "%snull", k ? ", " : "");
		}
		fprintf (f, "], \"wrap_s\": [");
		for (int k = 0; k < 8; k++)
		{
			int ti = d.mtex[i][k];
			fprintf (f, "%s\"%s\"", k ? ", " : "",
				(ti >= 0 && ti < d.num_tex) ? j3d_wrap_name (d.twrap_s[ti]) : "ClampToEdge");
		}
		fprintf (f, "], \"wrap_t\": [");
		for (int k = 0; k < 8; k++)
		{
			int ti = d.mtex[i][k];
			fprintf (f, "%s\"%s\"", k ? ", " : "",
				(ti >= 0 && ti < d.num_tex) ? j3d_wrap_name (d.twrap_t[ti]) : "ClampToEdge");
		}
		fprintf (f, "]}%s\n", i + 1 < d.num_mats ? "," : "");
	}
	fprintf (f, "  ]\n}\n");
	fclose (f);
	j3d_dec_free (&d);
	return 1;
}

int ExportJ3DTexHeadersJSON (const uint8_t *data, size_t size, const char *json_path)
{
	size_t o_inf1, o_vtx1, o_evp1, o_drw1, o_jnt1, o_shp1, o_mat3, o_mdl3, o_tex1;
	if (!j3d_find_sections (data, size, &o_inf1, &o_vtx1, &o_evp1, &o_drw1, &o_jnt1, &o_shp1,
			&o_mat3, &o_mdl3, &o_tex1, 0))
		return 0;
	j3d_dec_t d;
	memset (&d, 0, sizeof (d));
	if (!j3d_parse_tex1 (data, size, o_tex1, &d))
		return 0;
	FILE *f = fopen (json_path, "w");
	if (!f)
	{
		j3d_dec_free (&d);
		return 0;
	}
	fprintf (f, "{\n  \"generator\": \"nintoolbox-j3d\",\n  \"textures\": [\n");
	for (int i = 0; i < d.num_tex; i++)
	{
		fprintf (f, "    {\"name\": \"");
		j3d_json_escape (f, d.tnames[i]);
		fprintf (f,
			"\", \"format\": \"%s\", \"width\": %u, \"height\": %u, \"wrap_s\": \"%s\", "
			"\"wrap_t\": \"%s\", \"min_filter\": \"%s\", \"mag_filter\": \"%s\", "
			"\"mipmaps\": %u}",
			j3d_fmt_name (d.tfmt[i]), d.tw[i], d.th[i], j3d_wrap_name (d.twrap_s[i]),
			j3d_wrap_name (d.twrap_t[i]), j3d_filter_name (d.tminf[i]),
			j3d_filter_name (d.tmagf[i]), (uint)d.tmips[i] + 1);
		fprintf (f, "%s\n", i + 1 < d.num_tex ? "," : "");
	}
	fprintf (f, "  ]\n}\n");
	fclose (f);
	j3d_dec_free (&d);
	return 1;
}

void J3DProfileDump (const uint8_t *data, size_t size, FILE *f)
{
	if (!f)
		f = stdout;
	size_t o_inf1, o_vtx1, o_evp1, o_drw1, o_jnt1, o_shp1, o_mat3, o_mdl3, o_tex1;
	int is_bdl = 0;
	if (!j3d_find_sections (data, size, &o_inf1, &o_vtx1, &o_evp1, &o_drw1, &o_jnt1, &o_shp1,
			&o_mat3, &o_mdl3, &o_tex1, &is_bdl))
	{
		fprintf (f, "Not a J3D BMD/BDL file.\n");
		return;
	}
	uint32_t total = j3d_rd32 (data + 8);
	fprintf (f, "Type: J3D2%s (%s)\n",
		is_bdl								? "bdl4"
			: !memcmp (data + 4, "bmd2", 4) ? "bmd2"
											: "bmd3",
		is_bdl ? "BDL" : "BMD");
	fprintf (f, "Total size: %u bytes (%.1f KiB)\n", total, total / 1024.0);
	size_t sects[9] = { o_inf1, o_vtx1, o_evp1, o_drw1, o_jnt1, o_shp1, o_mat3, o_mdl3, o_tex1 };
	const char *names[9] = { "INF1 (scenegraph)", "VTX1 (vertices)", "EVP1 (envelopes)",
		"DRW1 (weights)", "JNT1 (joints)", "SHP1 (shapes)", "MAT3 (materials)", "MDL3 (displists)",
		"TEX1 (textures)" };
	for (int i = 0; i < 9; i++)
	{
		if (!sects[i])
		{
			if (i == 7 && !is_bdl)
				continue;
			fprintf (f, "Section %-18s missing\n", names[i]);
			continue;
		}
		uint32_t s = j3d_rd32 (data + sects[i] + 4);
		fprintf (f, "Section %-18s size: %7u bytes (%5.1f KiB, %5.2f%%)\n", names[i], s, s / 1024.0,
			total ? 100.0 * s / total : 0);
	}
	j3d_dec_t d;
	memset (&d, 0, sizeof (d));
	if (j3d_parse_inf1 (data, size, o_inf1, &d))
		fprintf (f, "INF1: %d scene nodes\n", d.num_nodes);
	if (j3d_parse_vtx1 (data, size, o_vtx1, &d))
	{
		fprintf (f, "Positions: %u  Normals: %u  Colors: %u/%u  UVs:", (uint)d.pos.n, (uint)d.nrm.n,
			(uint)d.c0.n, (uint)d.c1.n);
		for (int i = 0; i < 8; i++)
			if (d.has_tex[i])
				fprintf (f, " [%d]%u", i, (uint)d.tex[i].n);
		fprintf (f, "\n");
	}
	if (o_evp1 && j3d_parse_evp1 (data, size, o_evp1, &d))
		fprintf (f, "EVP1: %d envelopes, %d inverse-bind matrices\n", d.num_evp, d.num_inv);
	if (o_drw1 && j3d_parse_drw1 (data, size, o_drw1, &d))
		fprintf (f, "DRW1: %d slots\n", d.num_drw);
	j3d_dec_free (&d);
	memset (&d, 0, sizeof (d));
	if (!j3d_parse_inf1 (data, size, o_inf1, &d))
		fprintf (f, "INF1: parse failed\n");
	if (j3d_parse_jnt1 (data, size, o_jnt1, &d))
		fprintf (f, "JNT1: %d joints\n", d.num_joints_file);
	if (j3d_parse_mat3 (data, size, o_mat3, &d))
		fprintf (f, "MAT3: %d materials\n", d.num_mats);
	if (j3d_parse_tex1 (data, size, o_tex1, &d))
	{
		fprintf (f, "TEX1: %d textures\n", d.num_tex);
		for (int i = 0; i < d.num_tex; i++)
			fprintf (f, "  %d) %s  %s %ux%u  %u mips\n", i, d.tnames[i] ? d.tnames[i] : "?",
				j3d_fmt_name (d.tfmt[i]), d.tw[i], d.th[i], (uint)d.tmips[i] + 1);
	}
	j3d_dec_free (&d);
}

void SetupDefaultJ3DEncodeOpt (j3d_encode_opt_t *opt)
{
	memset (opt, 0, sizeof (*opt));
	opt->tristrip = 1; // static
}

// --- tiny JSON reader (our own sidecar schemas only) ---
typedef struct
{
	char key[64];
	char sval[256];
	double num;
	int is_num;
} j3d_kv_t;

static const char *j3d_skip_ws (const char *p, const char *end)
{
	while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
		p++;
	return p;
}
// parse "str" (with escapes) into out; returns pointer past closing quote
static const char *j3d_parse_str (const char *p, const char *end, char *out, size_t outsz)
{
	if (p >= end || *p != '"')
		return 0;
	p++;
	size_t n = 0;
	while (p < end && *p != '"')
	{
		char c = *p++;
		if (c == '\\' && p < end)
		{
			char e = *p++;
			c = e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e;
		}
		if (n + 1 < outsz)
			out[n++] = c;
	}
	out[n] = 0;
	return (p < end && *p == '"') ? p + 1 : 0;
}
// Find top-level array under key; returns [start,end) of array contents.
static int j3d_find_array (
	const char *json, size_t len, const char *key, const char **a0, const char **a1)
{
	char pat[80];
	snprintf (pat, sizeof (pat), "\"%s\"", key);
	const char *end = json + len;
	const char *p = json;
	size_t pl = strlen (pat);
	while (p + pl < end)
	{
		if (!memcmp (p, pat, pl))
		{
			const char *q = j3d_skip_ws (p + pl, end);
			if (q < end && *q == ':')
			{
				q = j3d_skip_ws (q + 1, end);
				if (q < end && *q == '[')
				{
					int depth = 0;
					const char *r = q;
					int instr = 0;
					while (r < end)
					{
						if (instr)
						{
							if (*r == '\\')
								r++;
							else if (*r == '"')
								instr = 0;
						}
						else if (*r == '"')
							instr = 1;
						else if (*r == '[')
							depth++;
						else if (*r == ']')
						{
							depth--;
							if (!depth)
							{
								*a0 = q + 1;
								*a1 = r;
								return 1;
							}
						}
						r++;
					}
					return 0;
				}
			}
		}
		p++;
	}
	return 0;
}
// Split array contents into top-level {...} object spans.
static int j3d_split_objects (
	const char *a0, const char *a1, const char **starts, const char **ends, int maxn)
{
	int n = 0;
	const char *p = a0;
	while (p < a1 && n < maxn)
	{
		p = j3d_skip_ws (p, a1);
		if (p >= a1 || *p != '{')
		{
			p++;
			continue;
		}
		int depth = 0, instr = 0;
		const char *r = p;
		while (r < a1)
		{
			if (instr)
			{
				if (*r == '\\')
					r++;
				else if (*r == '"')
					instr = 0;
			}
			else if (*r == '"')
				instr = 1;
			else if (*r == '{')
				depth++;
			else if (*r == '}')
			{
				depth--;
				if (!depth)
				{
					starts[n] = p;
					ends[n] = r + 1;
					n++;
					p = r + 1;
					break;
				}
			}
			r++;
		}
		if (r >= a1)
			break;
	}
	return n;
}
// Extract string value of key inside one object span. Returns 1 if found.
static int j3d_obj_str (const char *o0, const char *o1, const char *key, char *out, size_t outsz)
{
	char pat[80];
	snprintf (pat, sizeof (pat), "\"%s\"", key);
	size_t pl = strlen (pat);
	const char *p = o0;
	while (p + pl < o1)
	{
		if (!memcmp (p, pat, pl))
		{
			const char *q = j3d_skip_ws (p + pl, o1);
			if (q < o1 && *q == ':')
			{
				q = j3d_skip_ws (q + 1, o1);
				if (q < o1 && *q == '"')
					return j3d_parse_str (q, o1, out, outsz) != 0;
				return 0;
			}
		}
		p++;
	}
	return 0;
}
static int j3d_obj_num (const char *o0, const char *o1, const char *key, double *out)
{
	char pat[80];
	snprintf (pat, sizeof (pat), "\"%s\"", key);
	size_t pl = strlen (pat);
	const char *p = o0;
	while (p + pl < o1)
	{
		if (!memcmp (p, pat, pl))
		{
			const char *q = j3d_skip_ws (p + pl, o1);
			if (q < o1 && *q == ':')
			{
				q = j3d_skip_ws (q + 1, o1);
				char *e = 0;
				double v = strtod (q, &e);
				if (e != q)
				{
					*out = v;
					return 1;
				}
				return 0;
			}
		}
		p++;
	}
	return 0;
}
// Extract up-to-8 string-or-null array under key. null -> empty string + present=0.
static int j3d_obj_str8 (
	const char *o0, const char *o1, const char *key, char out[8][256], int present[8])
{
	const char *a0 = 0, *a1 = 0;
	char sub[65536];
	// scope the search to this object by copying span
	size_t sl = (size_t)(o1 - o0);
	if (sl >= sizeof (sub))
		return 0;
	memcpy (sub, o0, sl);
	sub[sl] = 0;
	if (!j3d_find_array (sub, sl, key, &a0, &a1))
		return 0;
	for (int i = 0; i < 8; i++)
	{
		out[i][0] = 0;
		present[i] = 0;
	}
	int idx = 0;
	const char *p = a0;
	while (p < a1 && idx < 8)
	{
		p = j3d_skip_ws (p, a1);
		if (p >= a1)
			break;
		if (!memcmp (p, "null", 4))
		{
			idx++;
			p += 4;
		}
		else if (*p == '"')
		{
			char tmp[256];
			const char *n = j3d_parse_str (p, a1, tmp, sizeof (tmp));
			if (!n)
				break;
			snprintf (out[idx], 256, "%s", tmp);
			present[idx] = 1;
			idx++;
			p = n;
		}
		else
			p++;
		if (p < a1 && *p == ',')
			p++;
	}
	return 1;
}
static int j3d_obj_numarr (const char *o0, const char *o1, const char *key, double *out, int maxn)
{
	const char *a0 = 0, *a1 = 0;
	char sub[4096];
	size_t sl = (size_t)(o1 - o0);
	if (sl >= sizeof (sub))
		return 0;
	memcpy (sub, o0, sl);
	sub[sl] = 0;
	if (!j3d_find_array (sub, sl, key, &a0, &a1))
		return 0;
	int idx = 0;
	const char *p = a0;
	while (p < a1 && idx < maxn)
	{
		p = j3d_skip_ws (p, a1);
		char *e = 0;
		double v = strtod (p, &e);
		if (e == p)
		{
			p++;
			continue;
		}
		out[idx++] = v;
		p = e;
		if (p < a1 && *p == ',')
			p++;
	}
	return idx;
}
static uint8_t *j3d_load_file (const char *path, size_t *size_out)
{
	FILE *f = fopen (path, "rb");
	if (!f)
		return 0;
	fseek (f, 0, SEEK_END);
	long n = ftell (f);
	fseek (f, 0, SEEK_SET);
	if (n < 0 || n > 64 * 1024 * 1024)
	{
		fclose (f);
		return 0;
	}
	uint8_t *b = MALLOC ((size_t)n + 1);
	if (!b)
	{
		fclose (f);
		return 0;
	}
	if (n && fread (b, 1, (size_t)n, f) != (size_t)n)
	{
		FREE (b);
		fclose (f);
		return 0;
	}
	b[n] = 0;
	fclose (f);
	if (size_out)
		*size_out = (size_t)n;
	return b;
}

// --- RGBA source loading (embedded PNG bytes or image files) ---
static uint8_t *j3d_rgba_from_image (Image_t *img, uint *w_out, uint *h_out)
{
	if (!img || !img->data || img->iform != IMG_X_RGB)
		return 0;
	uint w = img->width, h = img->height;
	if (!w || !h || w > 4096 || h > 4096)
		return 0;
	uint8_t *out = MALLOC ((size_t)w * h * 4);
	if (!out)
		return 0;
	for (uint y = 0; y < h; y++)
		memcpy (out + (size_t)y * w * 4, img->data + (size_t)y * img->xwidth * 4, (size_t)w * 4);
	*w_out = w;
	*h_out = h;
	return out;
}
static int j3d_try_load_image_file (const char *path, Image_t *img)
{
	memset (img, 0, sizeof (*img));
	enumError err = LoadIMG (img, true, path, 0, false, false, true);
	if (err != ERR_OK && err != ERR_WARNING)
		return 0;
	if (img->iform != IMG_X_RGB)
	{
		Image_t cv;
		memset (&cv, 0, sizeof (cv));
		if (ConvertIMG (&cv, true, img, IMG_X_RGB, PAL_INVALID) != ERR_OK)
		{
			ResetIMG (img);
			return 0;
		}
		ResetIMG (img);
		*img = cv;
	}
	return img->data != 0;
}
static const char *j3d_img_exts[4] = { ".png", ".jpg", ".tga", ".bmp" };
static int j3d_find_image_path (const char *dir, const char *base, int mip, char *out, size_t outsz)
{
	for (int e = 0; e < 4; e++)
	{
		if (mip < 0)
			snprintf (out, outsz, "%s/%s%s", dir, base, j3d_img_exts[e]);
		else
			snprintf (out, outsz, "%s/%s_mip%d%s", dir, base, mip + 1, j3d_img_exts[e]);
		struct stat st;
		if (!stat (out, &st))
			return 1;
	}
	return 0;
}

// --- encode context ---
typedef struct
{
	char name[64];
	uint8_t *rgba;
	uint w, h;
	int fmt;
	uint8_t wrap_s, wrap_t, minf, magf;
	int nmips;
	uint8_t **mip_rgba;
	uint *mip_w, *mip_h;
} j3d_etex_t;

typedef struct
{
	float p[3], n[3];
	int hasn;
	float uv[8][2];
	int hast[8];
	float c[2][4];
	int hasc[2];
	int nb; // 0 = rigid
	int bones[16];
	float weights[16];
} j3d_evert_t;

typedef struct
{
	int n; // unique weight count if multi, else -1
	int bones[16];
	float weights[16];
	int drw; // assigned DRW slot
} j3d_ew_t; // unique single (n=-1,bones[0]) or multi weight

typedef struct
{
	j3d_evert_t *v; // 3 per tri
	int ntris;
	int *slots; // DRW slots used
	int nslots;
	int *local; // local matrix idx per corner (ntris*3)
	int *rp, *rn, *rc0, *rc1, *rt[8]; // pool indices per corner
} j3d_epkt_t;

typedef struct
{
	mesh_t *src; // model mesh (borrowed)
	j3d_evert_t *ev; // expanded corners, nverts
	int nverts;
	j3d_epkt_t *pkts;
	int npkts;
	int home; // home bone
	int billboard; // 0 normal, 1 BillXY, 2 BillX
	int textured, hasvcol;
	int mat; // clamped material idx
} j3d_emesh_t;

typedef struct
{
	const model_t *model;
	const j3d_encode_opt_t *opt;
	char input_dir[PATH_MAX];
	// joints (synthetic root appended if model has none)
	int njoints;
	char (*jnames)[64];
	float *jtrs; // 9 floats: scale xyz euler xyz trans xyz
	int *jparent, *jmtx;
	float *jibm; // 12 floats each
	// weights
	j3d_ew_t *singles;
	int nsingles;
	j3d_ew_t *multis;
	int nmultis;
	// textures
	j3d_etex_t *tex;
	int ntex;
	// meshes
	j3d_emesh_t *em;
	int nem;
	// pools
	float *pos;
	int npos, cappos;
	j3d_map_t posmap;
	float *nrm;
	int nnrm, capnrm;
	j3d_map_t nrmmap;
	float *col[2];
	int ncol[2], capcol[2];
	j3d_map_t colmap[2];
	float *texp[8];
	int ntexp[8], captexp[8];
	j3d_map_t texmap[8];
	int texf32[8];
	// mat overrides from JSON
	float *mat_diffuse; // NULL or nmats*4
	int (*mat_texnames)[8]; // resolved TEX1 indices, -1; NULL if no JSON
	char (*mat_texstr)[8][64];
	uint8_t *mat_cull; // NULL or nmats
} j3d_enc_t;

static uint64_t j3d_bits_hash (const float *p, int n, uint64_t seed)
{
	uint64_t h = seed;
	for (int i = 0; i < n; i++)
	{
		uint32_t u;
		memcpy (&u, &p[i], 4);
		h ^= (uint64_t)u + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
	}
	return h ? h : 1;
}
static int j3d_pool_add (float **pool, int *n, int *cap, j3d_map_t *map, const float *v, int comps)
{
	uint64_t h = j3d_bits_hash (v, comps, 0x12345);
	int f = j3d_map_get (map, h);
	if (f >= 0)
		return f;
	int idx = *n;
	if (*n + 1 > *cap)
	{
		int nc = *cap ? *cap * 2 : 1024;
		*pool = REALLOC (*pool, (size_t)nc * comps * sizeof (float));
		if (!*pool)
			return -1;
		*cap = nc;
	}
	memcpy (*pool + (size_t)idx * comps, v, (size_t)comps * sizeof (float));
	(*n)++;
	j3d_map_put (map, h, idx);
	return idx;
}

// resolve one model position to (nb, bones, weights); drops zero weights
static void j3d_resolve_w (
	const model_t *model, const mesh_t *mesh, int posi, int *nb, int *bones, float *weights)
{
	*nb = 0;
	if (!mesh->position_node || posi < 0)
		return;
	int node = mesh->position_node[posi];
	if (node < 0 || (size_t)node >= model->num_node_influences)
		return;
	const node_influence_t *ni = &model->node_influences[node];
	for (size_t i = 0; i < ni->num_weights && *nb < 16; i++)
	{
		if (ni->weights[i].weight == 0.0f)
			continue;
		if (ni->weights[i].bone_idx < 0 || (size_t)ni->weights[i].bone_idx >= model->num_joints)
			continue;
		bones[*nb] = ni->weights[i].bone_idx;
		weights[*nb] = ni->weights[i].weight;
		(*nb)++;
	}
	// sort by bone for canonical form
	for (int i = 0; i < *nb; i++)
		for (int k = i + 1; k < *nb; k++)
			if (bones[k] < bones[i])
			{
				int tb = bones[i];
				bones[i] = bones[k];
				bones[k] = tb;
				float tw = weights[i];
				weights[i] = weights[k];
				weights[k] = tw;
			}
}
static int j3d_ew_same (const j3d_ew_t *a, int nb, const int *bones, const float *weights)
{
	if (a->n != nb)
		return 0;
	if (nb < 0)
		return a->bones[0] == bones[0];
	for (int i = 0; i < nb; i++)
		if (a->bones[i] != bones[i] || a->weights[i] != weights[i])
			return 0;
	return 1;
}

// IBM as 4x4 row-major from joint (rotate-aware handled by caller)
static void j3d_joint_ibm44 (const j3d_enc_t *e, int j, float m[16])
{
	if (j >= 0 && j < e->njoints)
		j3d_m4_from_34 (m, e->jibm + (size_t)j * 12);
	else
	{
		memset (m, 0, 16 * sizeof (float));
		m[0] = m[5] = m[10] = m[15] = 1.0f;
	}
}

// find embedded PNG bytes in model->images by texture name
static const model_image_t *j3d_find_embedded (const model_t *model, const char *name)
{
	char base[64];
	snprintf (base, sizeof (base), "%s", name);
	char *dot = strrchr (base, '.');
	if (dot)
		*dot = 0;
	for (size_t i = 0; i < model->num_images; i++)
	{
		const model_image_t *im = &model->images[i];
		if (!strcmp (im->name, name) || !strcmp (im->name, base))
			return im;
		char ib[64];
		snprintf (ib, sizeof (ib), "%s", im->name);
		char *d2 = strrchr (ib, '.');
		if (d2)
			*d2 = 0;
		if (!strcmp (ib, base) || !strcmp (ib, name))
			return im;
	}
	return 0;
}
// load PNG bytes (memory or file) -> RGBA
static uint8_t *j3d_rgba_from_png_bytes (const uint8_t *png, size_t png_size, uint *w, uint *h)
{
	if (!png || !png_size)
		return 0;
	char tmp[PATH_MAX];
	snprintf (tmp, sizeof (tmp), "/tmp/nintoolbox_j3d_%d.png", (int)getpid ());
	FILE *f = fopen (tmp, "wb");
	if (!f)
		return 0;
	size_t wr = fwrite (png, 1, png_size, f);
	fclose (f);
	uint8_t *out = 0;
	if (wr == png_size)
	{
		Image_t img;
		if (j3d_try_load_image_file (tmp, &img))
		{
			out = j3d_rgba_from_image (&img, w, h);
			ResetIMG (&img);
		}
	}
	unlink (tmp);
	return out;
}
// collect one texture (base + optional mips) into e->tex; returns index
static int j3d_collect_texture (j3d_enc_t *e, const char *name)
{
	for (int i = 0; i < e->ntex; i++)
		if (!strcmp (e->tex[i].name, name))
			return i;
	j3d_etex_t t;
	memset (&t, 0, sizeof (t));
	snprintf (t.name, sizeof (t.name), "%s", name);
	t.fmt = J3D_TEX_CMPR;
	t.wrap_s = t.wrap_t = 1;
	t.minf = t.magf = 1;
	// (a) embedded
	const model_image_t *emb = j3d_find_embedded (e->model, name);
	if (emb && emb->data && emb->size > 8 && !memcmp (emb->data, "\x89PNG", 4))
		t.rgba = j3d_rgba_from_png_bytes (emb->data, emb->size, &t.w, &t.h);
	// (b) files: tex_dir, input dir
	if (!t.rgba)
	{
		const char *dirs[2] = { e->opt->tex_dir ? e->opt->tex_dir : e->input_dir, e->input_dir };
		for (int dd = 0; dd < 2 && !t.rgba; dd++)
		{
			char path[PATH_MAX];
			if (!j3d_find_image_path (dirs[dd], name, -1, path, sizeof (path)))
				continue;
			Image_t img;
			if (j3d_try_load_image_file (path, &img))
			{
				t.rgba = j3d_rgba_from_image (&img, &t.w, &t.h);
				ResetIMG (&img);
			}
		}
	}
	if (!t.rgba)
	{
		// missing texture: opaque white 4x4 (SuperBMD --tphd behavior)
		t.w = t.h = 4;
		t.rgba = CALLOC (64, 1);
		if (t.rgba)
			memset (t.rgba, 255, 64);
		fprintf (stderr, "j3d: warning: texture \"%s\" not found, using white\n", name);
	}
	// mips from files
	if (!e->opt->no_mipmaps && t.rgba)
	{
		const char *dirs[2] = { e->opt->tex_dir ? e->opt->tex_dir : e->input_dir, e->input_dir };
		for (int m = 0; m < 12; m++)
		{
			uint mw = t.w >> (m + 1), mh = t.h >> (m + 1);
			if (!mw)
				mw = 1;
			if (!mh)
				mh = 1;
			char path[PATH_MAX];
			int found = 0;
			for (int dd = 0; dd < 2 && !found; dd++)
				found = j3d_find_image_path (dirs[dd], name, m, path, sizeof (path));
			if (!found)
				break;
			Image_t img;
			uint iw = 0, ih = 0;
			uint8_t *px = 0;
			if (j3d_try_load_image_file (path, &img))
			{
				px = j3d_rgba_from_image (&img, &iw, &ih);
				ResetIMG (&img);
			}
			if (!px || iw != mw || ih != mh)
			{
				FREE (px);
				if (px)
					fprintf (stderr,
						"j3d: warning: mipmap dimension mismatch for %s level %d "
						"(expected %ux%u)\n",
						name, m + 1, mw, mh);
				break;
			}
			t.mip_rgba = REALLOC (t.mip_rgba, (size_t)(t.nmips + 1) * sizeof (uint8_t *));
			t.mip_w = REALLOC (t.mip_w, (size_t)(t.nmips + 1) * sizeof (uint));
			t.mip_h = REALLOC (t.mip_h, (size_t)(t.nmips + 1) * sizeof (uint));
			if (!t.mip_rgba || !t.mip_w || !t.mip_h)
			{
				FREE (px);
				break;
			}
			t.mip_rgba[t.nmips] = px;
			t.mip_w[t.nmips] = mw;
			t.mip_h[t.nmips] = mh;
			t.nmips++;
		}
		if (t.nmips)
		{
			t.minf = 5; // LinearMipmapLinear
		}
	}
	e->tex = REALLOC (e->tex, (size_t)(e->ntex + 1) * sizeof (*e->tex));
	if (!e->tex)
	{
		FREE (t.rgba);
		return -1;
	}
	e->tex[e->ntex] = t;
	return e->ntex++;
}
// apply --texheader JSON overrides to collected textures
static void j3d_apply_texheaders (j3d_enc_t *e, const char *json_path)
{
	size_t len = 0;
	uint8_t *json = j3d_load_file (json_path, &len);
	if (!json)
	{
		fprintf (stderr, "j3d: warning: cannot read texheader file %s\n", json_path);
		return;
	}
	const char *a0 = 0, *a1 = 0;
	if (!j3d_find_array ((char *)json, len, "textures", &a0, &a1))
	{
		FREE (json);
		return;
	}
	const char *starts[4096], *ends[4096];
	int n = j3d_split_objects (a0, a1, starts, ends, 4096);
	for (int i = 0; i < n; i++)
	{
		char name[256] = "", fmt[32] = "", ws[32] = "", wt[32] = "", mn[40] = "", mg[40] = "";
		if (!j3d_obj_str (starts[i], ends[i], "name", name, sizeof (name)))
			continue;
		int ti = -1;
		for (int k = 0; k < e->ntex; k++)
			if (!strcmp (e->tex[k].name, name))
				ti = k;
		if (ti < 0)
			continue;
		if (j3d_obj_str (starts[i], ends[i], "format", fmt, sizeof (fmt)))
		{
			int f = -1;
			if (!strcmp (fmt, "I4"))
				f = J3D_TEX_I4;
			else if (!strcmp (fmt, "I8"))
				f = J3D_TEX_I8;
			else if (!strcmp (fmt, "IA4"))
				f = J3D_TEX_IA4;
			else if (!strcmp (fmt, "IA8"))
				f = J3D_TEX_IA8;
			else if (!strcmp (fmt, "RGB565"))
				f = J3D_TEX_RGB565;
			else if (!strcmp (fmt, "RGB5A3"))
				f = J3D_TEX_RGB5A3;
			else if (!strcmp (fmt, "RGBA32"))
				f = J3D_TEX_RGBA32;
			else if (!strcmp (fmt, "C4"))
				f = J3D_TEX_C4;
			else if (!strcmp (fmt, "C8"))
				f = J3D_TEX_C8;
			else if (!strcmp (fmt, "CMPR"))
				f = J3D_TEX_CMPR;
			if (f == J3D_TEX_RGBA32 || f == J3D_TEX_CMPR)
				e->tex[ti].fmt = f;
			else if (f >= 0)
				fprintf (stderr,
					"j3d: warning: paletted encode to %s unsupported, keeping %s for %s\n", fmt,
					e->tex[ti].fmt == J3D_TEX_CMPR ? "CMPR" : "RGBA32", name);
		}
		if (j3d_obj_str (starts[i], ends[i], "wrap_s", ws, sizeof (ws)))
			e->tex[ti].wrap_s = !strcmp (ws, "Repeat") ? 1 : !strcmp (ws, "MirroredRepeat") ? 2 : 0;
		if (j3d_obj_str (starts[i], ends[i], "wrap_t", wt, sizeof (wt)))
			e->tex[ti].wrap_t = !strcmp (wt, "Repeat") ? 1 : !strcmp (wt, "MirroredRepeat") ? 2 : 0;
		if (j3d_obj_str (starts[i], ends[i], "min_filter", mn, sizeof (mn)))
		{
			if (!strcmp (mn, "Nearest"))
				e->tex[ti].minf = 0;
			else if (!strcmp (mn, "Linear"))
				e->tex[ti].minf = 1;
			else if (!strcmp (mn, "NearestMipmapNearest"))
				e->tex[ti].minf = 2;
			else if (!strcmp (mn, "NearestMipmapLinear"))
				e->tex[ti].minf = 3;
			else if (!strcmp (mn, "LinearMipmapNearest"))
				e->tex[ti].minf = 4;
			else if (!strcmp (mn, "LinearMipmapLinear"))
				e->tex[ti].minf = 5;
		}
		if (j3d_obj_str (starts[i], ends[i], "mag_filter", mg, sizeof (mg)))
			e->tex[ti].magf = !strcmp (mg, "Nearest") ? 0 : 1;
	}
	FREE (json);
}
// apply --mat JSON overrides (diffuse, textures, cull); supports __MatDefault
static void j3d_apply_materials_json (j3d_enc_t *e, const char *json_path, int nmats)
{
	size_t len = 0;
	uint8_t *json = j3d_load_file (json_path, &len);
	if (!json)
	{
		fprintf (stderr, "j3d: warning: cannot read material file %s\n", json_path);
		return;
	}
	const char *a0 = 0, *a1 = 0;
	if (!j3d_find_array ((char *)json, len, "materials", &a0, &a1))
	{
		FREE (json);
		return;
	}
	e->mat_diffuse = CALLOC ((size_t)nmats * 4, sizeof (float));
	e->mat_texstr = CALLOC ((size_t)nmats, sizeof (*e->mat_texstr));
	e->mat_cull = CALLOC ((size_t)nmats, sizeof (uint8_t));
	e->mat_texnames = CALLOC ((size_t)nmats, sizeof (*e->mat_texnames));
	if (!e->mat_diffuse || !e->mat_texstr || !e->mat_cull || !e->mat_texnames)
	{
		FREE (json);
		return;
	}
	for (int i = 0; i < nmats; i++)
	{
		for (int k = 0; k < 8; k++)
			e->mat_texnames[i][k] = -2; // -2 = keep model binding
		e->mat_cull[i] = 0xff; // keep
		for (int k = 0; k < 4; k++)
			e->mat_diffuse[i * 4 + k] = -1.0f; // keep
	}
	const char *starts[4096], *ends[4096];
	int n = j3d_split_objects (a0, a1, starts, ends, 4096);
	// default entry first
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < n; i++)
		{
			char name[256] = "";
			if (!j3d_obj_str (starts[i], ends[i], "name", name, sizeof (name)))
				continue;
			int is_def = !strcmp (name, "__MatDefault");
			if ((pass == 0) != is_def)
				continue;
			for (int m = 0; m < nmats; m++)
			{
				const char *mn = e->model->materials[m].name;
				if (!is_def && strcmp (mn, name))
					continue;
				if (!is_def || e->mat_cull[m] == 0xff)
				{
					char cull[16] = "";
					if (j3d_obj_str (starts[i], ends[i], "cull", cull, sizeof (cull)))
						e->mat_cull[m] = !strcmp (cull, "None") ? 0
							: !strcmp (cull, "Front")			? 1
							: !strcmp (cull, "All")				? 3
																: 2;
					double dv[4];
					if (j3d_obj_numarr (starts[i], ends[i], "diffuse", dv, 4) == 4)
						for (int k = 0; k < 4; k++)
							e->mat_diffuse[m * 4 + k] = (float)dv[k];
					char tx[8][256];
					int pr[8];
					if (j3d_obj_str8 (starts[i], ends[i], "textures", tx, pr))
						for (int k = 0; k < 8; k++)
						{
							if (!pr[k])
							{
								e->mat_texstr[m][k][0] = 0;
								e->mat_texnames[m][k] = -1;
							}
							else if (tx[k][0])
							{
								snprintf (e->mat_texstr[m][k], 64, "%s", tx[k]);
								e->mat_texnames[m][k] = -3; // resolve later
							}
						}
				}
				if (!is_def)
					break;
			}
		}
	FREE (json);
}

// rotation (x,y,z)->(x,z,-y) applied to world-space verts; IBMs pre-rotated.
static void j3d_apply_rotate_pt (float p[3])
{
	float y = p[1], z = p[2];
	p[1] = z;
	p[2] = -y;
}
// R^-1 x IBM for row-vector convention; R maps (x,y,z)->(x,z,-y).
static void j3d_rotate_ibm (const float in[12], float out[12])
{
	// Rinv rows: [1,0,0],[0,0,1],[0,-1,0] => out[i] = sum_k Rinv[i][k]*IBM[k]
	float m[16], r[16], res[16];
	j3d_m4_from_34 (m, in);
	memset (r, 0, sizeof (r));
	r[0] = 1;
	r[6] = 1;
	r[9] = -1;
	r[15] = 1;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			res[i * 4 + j] = r[i * 4] * m[j] + r[i * 4 + 1] * m[4 + j] + r[i * 4 + 2] * m[8 + j]
				+ r[i * 4 + 3] * m[12 + j];
	for (int i = 0; i < 12; i++)
		out[i] = res[i];
}

static int j3d_expand_mesh (j3d_enc_t *e, int mi)
{
	const model_t *model = e->model;
	const mesh_t *src = &model->meshes[mi];
	j3d_emesh_t *em = &e->em[mi];
	em->src = (mesh_t *)src;
	em->mat = src->material_idx;
	if (em->mat < 0 || (size_t)em->mat >= model->num_materials)
		em->mat = model->num_materials ? 0 : -1;
	em->nverts = (int)src->num_vertices;
	if (em->nverts <= 0 || em->nverts % 3 != 0)
		return 0;
	em->ev = CALLOC ((size_t)em->nverts, sizeof (*em->ev));
	if (!em->ev)
		return 0;
	float ibm[16], ibm33inv[16], m3[16];
	for (int i = 0; i < em->nverts; i++)
	{
		const vertex_t *v = &src->vertices[i];
		j3d_evert_t *c = &em->ev[i];
		int pi = v->position_idx;
		if (pi < 0 || (size_t)pi >= src->num_positions)
			return 0;
		c->p[0] = src->positions[pi].x;
		c->p[1] = src->positions[pi].y;
		c->p[2] = src->positions[pi].z;
		c->hasn = 0;
		if (v->normal_idx >= 0 && (size_t)v->normal_idx < src->num_normals && src->normals)
		{
			c->n[0] = src->normals[v->normal_idx].x;
			c->n[1] = src->normals[v->normal_idx].y;
			c->n[2] = src->normals[v->normal_idx].z;
			c->hasn = 1;
		}
		for (int t = 0; t < 8; t++)
		{
			c->hast[t] = 0;
			int ti = t == 0 ? v->texcoord_idx : v->extra_texcoord_idx[t - 1];
			size_t nn = t == 0 ? src->num_texcoords : src->num_extra_texcoords[t - 1];
			const vec2_t *pool = t == 0 ? src->texcoords : src->extra_texcoords[t - 1];
			if (ti >= 0 && (size_t)ti < nn && pool)
			{
				c->uv[t][0] = pool[ti].u;
				c->uv[t][1] = pool[ti].v;
				c->hast[t] = 1;
			}
		}
		for (int t = 0; t < 2; t++)
		{
			c->hasc[t] = 0;
			if (v->color_idx[t] >= 0 && (size_t)v->color_idx[t] < src->num_colors[t]
				&& src->colors[t])
			{
				c->c[t][0] = src->colors[t][v->color_idx[t]].r;
				c->c[t][1] = src->colors[t][v->color_idx[t]].g;
				c->c[t][2] = src->colors[t][v->color_idx[t]].b;
				c->c[t][3] = src->colors[t][v->color_idx[t]].a;
				c->hasc[t] = 1;
			}
		}
		j3d_resolve_w (model, src, pi, &c->nb, c->bones, c->weights);
		if (e->opt->rotate_model)
		{
			j3d_apply_rotate_pt (c->p);
			if (c->hasn)
				j3d_apply_rotate_pt (c->n);
		}
		// bake single-weight verts into bone space
		if (c->nb == 1)
		{
			int b = c->bones[0];
			j3d_joint_ibm44 (e, b, ibm);
			float p[3];
			memcpy (p, c->p, sizeof (p));
			j3d_xform_pt (ibm, p, c->p);
			if (c->hasn)
			{
				memcpy (m3, ibm, sizeof (m3));
				m3[3] = m3[7] = m3[11] = m3[12] = m3[13] = m3[14] = 0.0f;
				m3[15] = 1.0f;
				if (j3d_m4_invert (ibm33inv, m3))
				{
					float n[3];
					memcpy (n, c->n, sizeof (n));
					j3d_xform_nrm33 (ibm33inv, n, c->n);
				}
			}
		}
	}
	// home bone
	em->home = 0;
	{
		int hb = -1, ok = 1;
		for (int i = 0; i < em->nverts; i++)
		{
			if (!em->ev[i].nb)
				continue;
			if (em->ev[i].nb != 1)
			{
				ok = 0;
				break;
			}
			if (hb < 0)
				hb = em->ev[i].bones[0];
			else if (hb != em->ev[i].bones[0])
			{
				ok = 0;
				break;
			}
		}
		if (ok && hb >= 0)
			em->home = hb;
	}
	// textured / vcol flags (post JSON override)
	em->textured = 0;
	em->hasvcol = 0;
	if (em->mat >= 0)
	{
		if (e->mat_texnames)
		{
			for (int k = 0; k < 8; k++)
				if (e->mat_texnames[em->mat][k] >= 0
					|| (e->mat_texnames[em->mat][k] == -2
						&& k < model->materials[em->mat].num_textures))
					em->textured = 1;
		}
		else if (model->materials[em->mat].num_textures > 0)
			em->textured = 1;
	}
	for (int i = 0; i < em->nverts; i++)
		if (em->ev[i].hasc[0])
			em->hasvcol = 1;
	// billboard via name suffix (SuperBMD convention)
	em->billboard = 0;
	if (strstr (src->name, "BillXY"))
		em->billboard = 1;
	else if (strstr (src->name, "BillX"))
		em->billboard = 2;
	return 1;
}

// collect global single/multi weight lists; assigns DRW slots
static void j3d_collect_weights (j3d_enc_t *e)
{
	for (int mi = 0; mi < e->nem; mi++)
	{
		j3d_emesh_t *em = &e->em[mi];
		for (int i = 0; i < em->nverts; i++)
		{
			j3d_evert_t *c = &em->ev[i];
			if (!c->nb)
				continue; // rigid -> root single, ensured below
			if (c->nb == 1)
			{
				int f = -1;
				for (int k = 0; k < e->nsingles; k++)
					if (e->singles[k].bones[0] == c->bones[0])
						f = k;
				if (f < 0)
				{
					e->singles
						= REALLOC (e->singles, (size_t)(e->nsingles + 1) * sizeof (*e->singles));
					if (!e->singles)
						return;
					e->singles[e->nsingles].n = -1;
					e->singles[e->nsingles].bones[0] = c->bones[0];
					e->singles[e->nsingles].drw = e->nsingles;
					e->nsingles++;
				}
			}
			else
			{
				int f = -1;
				for (int k = 0; k < e->nmultis; k++)
					if (j3d_ew_same (&e->multis[k], c->nb, c->bones, c->weights))
						f = k;
				if (f < 0)
				{
					e->multis = REALLOC (e->multis, (size_t)(e->nmultis + 1) * sizeof (*e->multis));
					if (!e->multis)
						return;
					e->multis[e->nmultis].n = c->nb;
					memcpy (e->multis[e->nmultis].bones, c->bones, sizeof (int) * (size_t)c->nb);
					memcpy (
						e->multis[e->nmultis].weights, c->weights, sizeof (float) * (size_t)c->nb);
					e->nmultis++;
				}
			}
		}
	}
	// ensure root single exists for rigid verts
	int has_root = 0;
	for (int k = 0; k < e->nsingles; k++)
		if (e->singles[k].bones[0] == 0)
			has_root = 1;
	int need_root = !has_root;
	if (!need_root)
		for (int mi = 0; mi < e->nem && !need_root; mi++)
			for (int i = 0; i < e->em[mi].nverts; i++)
				if (!e->em[mi].ev[i].nb)
					need_root = 1;
	if (need_root && !has_root)
	{
		e->singles = REALLOC (e->singles, (size_t)(e->nsingles + 1) * sizeof (*e->singles));
		if (e->singles)
		{
			// root single first (matches SuperBMD-ish root priority)
			memmove (e->singles + 1, e->singles, (size_t)e->nsingles * sizeof (*e->singles));
			e->singles[0].n = -1;
			e->singles[0].bones[0] = 0;
			e->nsingles++;
		}
	}
	for (int k = 0; k < e->nsingles; k++)
		e->singles[k].drw = k;
	for (int k = 0; k < e->nmultis; k++)
		e->multis[k].drw = e->nsingles + k;
}
static int j3d_corner_drw (j3d_enc_t *e, const j3d_evert_t *c)
{
	if (!c->nb)
	{
		for (int k = 0; k < e->nsingles; k++)
			if (e->singles[k].bones[0] == 0)
				return e->singles[k].drw;
		return 0;
	}
	if (c->nb == 1)
	{
		for (int k = 0; k < e->nsingles; k++)
			if (e->singles[k].bones[0] == c->bones[0])
				return e->singles[k].drw;
		return 0;
	}
	for (int k = 0; k < e->nmultis; k++)
		if (j3d_ew_same (&e->multis[k], c->nb, c->bones, c->weights))
			return e->multis[k].drw;
	return 0;
}

// greedy packets (<=10 unique DRW slots), pool insertion, per-corner refs
typedef struct
{
	int p, n, c[2], t[8];
	int local;
} j3d_cref_t;

static int j3d_build_packets (j3d_enc_t *e, int mi)
{
	j3d_emesh_t *em = &e->em[mi];
	int has_pmtx = e->njoints > 1;
	if (has_pmtx)
	{
		int bones[64];
		int nb = 0;
		for (int i = 0; i < em->nverts && nb < 64; i++)
		{
			j3d_evert_t *c = &em->ev[i];
			if (!c->nb)
				continue;
			for (int k = 0; k < c->nb; k++)
			{
				int f = 0;
				for (int q = 0; q < nb; q++)
					if (bones[q] == c->bones[k])
						f = 1;
				if (!f && nb < 64)
					bones[nb++] = c->bones[k];
			}
		}
		if (nb <= 1)
			has_pmtx = 0;
	}
	int ntris = em->nverts / 3;
	int *slot_of = MALLOC ((size_t)em->nverts * sizeof (int));
	if (!slot_of)
		return 0;
	for (int i = 0; i < em->nverts; i++)
		slot_of[i] = j3d_corner_drw (e, &em->ev[i]);
	// greedy packet split
	int cap = 4;
	em->pkts = MALLOC ((size_t)cap * sizeof (*em->pkts));
	em->npkts = 0;
	if (!em->pkts)
	{
		FREE (slot_of);
		return 0;
	}
	int t = 0;
	while (t < ntris)
	{
		if (em->npkts >= cap)
		{
			cap *= 2;
			em->pkts = REALLOC (em->pkts, (size_t)cap * sizeof (*em->pkts));
			if (!em->pkts)
			{
				FREE (slot_of);
				return 0;
			}
		}
		j3d_epkt_t *pk = &em->pkts[em->npkts];
		memset (pk, 0, sizeof (*pk));
		int used[10], nused = 0;
		while (t < ntris)
		{
			int need[3] = { slot_of[t * 3], slot_of[t * 3 + 1], slot_of[t * 3 + 2] };
			int add = 0;
			for (int k = 0; k < 3; k++)
			{
				int f = 0;
				for (int q = 0; q < nused; q++)
					if (used[q] == need[k])
						f = 1;
				if (!f)
					add++;
			}
			if (nused + add > 10 || pk->ntris >= 21845)
				break;
			for (int k = 0; k < 3; k++)
			{
				int f = 0;
				for (int q = 0; q < nused; q++)
					if (used[q] == need[k])
						f = 1;
				if (!f && nused < 10)
					used[nused++] = need[k];
			}
			pk->ntris++;
			t++;
		}
		pk->slots = MALLOC ((size_t)(nused ? nused : 1) * sizeof (int));
		if (!pk->slots)
		{
			FREE (slot_of);
			return 0;
		}
		memcpy (pk->slots, used, (size_t)nused * sizeof (int));
		pk->nslots = nused;
		em->npkts++;
	}
	// pool insertion + corner refs
	int vbase = 0;
	for (int pi = 0; pi < em->npkts; pi++)
	{
		j3d_epkt_t *pk = &em->pkts[pi];
		pk->local = MALLOC ((size_t)pk->ntris * 3 * sizeof (int));
		pk->rp = MALLOC ((size_t)pk->ntris * 3 * sizeof (int));
		pk->rn = MALLOC ((size_t)pk->ntris * 3 * sizeof (int));
		pk->rc0 = MALLOC ((size_t)pk->ntris * 3 * sizeof (int));
		pk->rc1 = MALLOC ((size_t)pk->ntris * 3 * sizeof (int));
		for (int k = 0; k < 8; k++)
			pk->rt[k] = MALLOC ((size_t)pk->ntris * 3 * sizeof (int));
		if (!pk->local || !pk->rp || !pk->rn || !pk->rc0 || !pk->rc1)
		{
			FREE (slot_of);
			return 0;
		}
		for (int i = 0; i < pk->ntris * 3; i++)
		{
			int vi = (vbase + i);
			j3d_evert_t *c = &em->ev[vi];
			int li = 0;
			for (; li < pk->nslots; li++)
				if (pk->slots[li] == slot_of[vi])
					break;
			if (li >= pk->nslots)
				li = 0;
			pk->local[i] = has_pmtx ? li : 0;
			// pools (dedup exact)
			float v3[3];
			v3[0] = c->p[0];
			v3[1] = c->p[1];
			v3[2] = c->p[2];
			pk->rp[i] = j3d_pool_add (&e->pos, &e->npos, &e->cappos, &e->posmap, v3, 3);
			pk->rn[i]
				= c->hasn ? j3d_pool_add (&e->nrm, &e->nnrm, &e->capnrm, &e->nrmmap, c->n, 3) : -1;
			pk->rc0[i] = c->hasc[0]
				? j3d_pool_add (&e->col[0], &e->ncol[0], &e->capcol[0], &e->colmap[0], c->c[0], 4)
				: -1;
			pk->rc1[i] = c->hasc[1]
				? j3d_pool_add (&e->col[1], &e->ncol[1], &e->capcol[1], &e->colmap[1], c->c[1], 4)
				: -1;
			for (int k = 0; k < 8; k++)
				pk->rt[k][i] = c->hast[k] ? j3d_pool_add (&e->texp[k], &e->ntexp[k], &e->captexp[k],
												&e->texmap[k], c->uv[k], 2)
										  : -1;
		}
		vbase += pk->ntris * 3;
	}
	FREE (slot_of);
	// stash pmtx flag in home high bits? no: recompute at write from same rule
	(void)has_pmtx;
	return 1;
}
// re-derive "has pmtx" identically to j3d_build_packets
static int j3d_mesh_has_pmtx (j3d_enc_t *e, int mi)
{
	if (e->njoints <= 1)
		return 0;
	j3d_emesh_t *em = &e->em[mi];
	int bones[64], nb = 0;
	for (int i = 0; i < em->nverts && nb < 64; i++)
	{
		j3d_evert_t *c = &em->ev[i];
		if (!c->nb)
			continue;
		for (int k = 0; k < c->nb; k++)
		{
			int f = 0;
			for (int q = 0; q < nb; q++)
				if (bones[q] == c->bones[k])
					f = 1;
			if (!f && nb < 64)
				bones[nb++] = c->bones[k];
		}
	}
	return nb > 1;
}

// --- encode: section writers ---
static void j3d_enc_bounds (j3d_enc_t *e, int mi, float *mn, float *mx)
{
	j3d_emesh_t *em = &e->em[mi];
	mn[0] = mn[1] = mn[2] = 1e30f;
	mx[0] = mx[1] = mx[2] = -1e30f;
	for (int pi = 0; pi < em->npkts; pi++)
	{
		j3d_epkt_t *pk = &em->pkts[pi];
		for (int i = 0; i < pk->ntris * 3; i++)
		{
			const float *p = e->pos + (size_t)pk->rp[i] * 3;
			for (int k = 0; k < 3; k++)
			{
				if (p[k] < mn[k])
					mn[k] = p[k];
				if (p[k] > mx[k])
					mx[k] = p[k];
			}
		}
	}
	if (mn[0] > mx[0])
		mn[0] = mn[1] = mn[2] = mx[0] = mx[1] = mx[2] = 0.0f;
}
static float j3d_bound_radius (const float *mn, const float *mx)
{
	float c[3];
	for (int k = 0; k < 3; k++)
		c[k] = (mn[k] + mx[k]) * 0.5f;
	float dx = mx[0] - c[0], dy = mx[1] - c[1], dz = mx[2] - c[2];
	return sqrtf (dx * dx + dy * dy + dz * dz);
}

// INF1 node stream
typedef struct
{
	int16_t *t, *x;
	int n, cap;
} j3d_nodes_t;
static void j3d_nodes_push (j3d_nodes_t *ns, int type, int idx)
{
	if (ns->n >= ns->cap)
	{
		int nc = ns->cap ? ns->cap * 2 : 64;
		ns->t = REALLOC (ns->t, (size_t)nc * 2);
		ns->x = REALLOC (ns->x, (size_t)nc * 2);
		if (!ns->t || !ns->x)
			return;
		ns->cap = nc;
	}
	ns->t[ns->n] = (int16_t)type;
	ns->x[ns->n] = (int16_t)idx;
	ns->n++;
}
static void j3d_inf1_recurse (j3d_enc_t *e, j3d_nodes_t *ns, int bone, int *down)
{
	j3d_nodes_push (ns, J3D_NODE_JOINT, bone);
	for (int mi = 0; mi < e->nem; mi++)
	{
		if (e->em[mi].home != bone)
			continue;
		j3d_nodes_push (ns, J3D_NODE_OPEN, 0);
		j3d_nodes_push (ns, J3D_NODE_MAT, e->em[mi].mat < 0 ? 0 : e->em[mi].mat);
		j3d_nodes_push (ns, J3D_NODE_OPEN, 0);
		j3d_nodes_push (ns, J3D_NODE_SHAPE, mi);
		*down += 2;
	}
	// children
	int has_kids = 0;
	for (int j = 0; j < e->njoints; j++)
		if (e->jparent[j] == bone)
			has_kids = 1;
	if (has_kids)
	{
		j3d_nodes_push (ns, J3D_NODE_OPEN, 0);
		for (int j = 0; j < e->njoints; j++)
			if (e->jparent[j] == bone)
				j3d_inf1_recurse (e, ns, j, down);
		j3d_nodes_push (ns, J3D_NODE_CLOSE, 0);
	}
}
static void j3d_write_inf1 (j3d_enc_t *e, j3d_buf_t *out, int *out_packets)
{
	j3d_nodes_t ns;
	memset (&ns, 0, sizeof (ns));
	j3d_nodes_push (&ns, J3D_NODE_JOINT, 0);
	int down = 0;
	for (int mi = 0; mi < e->nem; mi++)
	{
		if (e->em[mi].home != 0)
			continue; // single-bone non-root: emitted under its bone
		j3d_nodes_push (&ns, J3D_NODE_OPEN, 0);
		j3d_nodes_push (&ns, J3D_NODE_MAT, e->em[mi].mat < 0 ? 0 : e->em[mi].mat);
		j3d_nodes_push (&ns, J3D_NODE_OPEN, 0);
		j3d_nodes_push (&ns, J3D_NODE_SHAPE, mi);
		down += 2;
	}
	if (e->njoints > 1)
	{
		j3d_nodes_push (&ns, J3D_NODE_OPEN, 0);
		for (int j = 0; j < e->njoints; j++)
			if (e->jparent[j] == 0)
				j3d_inf1_recurse (e, &ns, j, &down);
		j3d_nodes_push (&ns, J3D_NODE_CLOSE, 0);
	}
	for (int k = 0; k < down; k++)
		j3d_nodes_push (&ns, J3D_NODE_CLOSE, 0);
	j3d_nodes_push (&ns, J3D_NODE_TERM, 0);
	int npk = 0;
	for (int mi = 0; mi < e->nem; mi++)
		npk += e->em[mi].npkts;
	*out_packets = npk;
	size_t start = out->size;
	j3d_wbytes (out, "INF1", 4);
	j3d_w32 (out, 0);
	j3d_ws16 (out, 2); // Xsi transform mode
	j3d_ws16 (out, -1);
	j3d_w32 (out, (uint32_t)npk);
	j3d_w32 (out, (uint32_t)e->npos);
	j3d_w32 (out, 0x18);
	for (int i = 0; i < ns.n; i++)
	{
		j3d_ws16 (out, ns.t[i]);
		j3d_ws16 (out, ns.x[i]);
	}
	FREE (ns.t);
	FREE (ns.x);
	j3d_wpad (out, 32, 0);
	j3d_patch32 (out, start + 4, (uint32_t)(out->size - start));
}

static void j3d_write_vtx1 (j3d_enc_t *e, j3d_buf_t *out)
{
	int has_nrm = e->nnrm > 0, has_c[2] = { e->ncol[0] > 0, e->ncol[1] > 0 }, has_t[8];
	for (int k = 0; k < 8; k++)
		has_t[k] = e->ntexp[k] > 0;
	size_t start = out->size;
	j3d_wbytes (out, "VTX1", 4);
	j3d_w32 (out, 0);
	j3d_w32 (out, 0x40);
	size_t offpos = out->size;
	for (int i = 0; i < 13; i++)
		j3d_w32 (out, 0);
	// attr headers
	j3d_w32 (out, 9);
	j3d_w32 (out, 1);
	j3d_w32 (out, 4);
	j3d_w8 (out, 0);
	j3d_w8 (out, 0xff);
	j3d_w16 (out, 0xffff);
	if (has_nrm)
	{
		j3d_w32 (out, 10);
		j3d_w32 (out, 0);
		j3d_w32 (out, 3);
		j3d_w8 (out, 14);
		j3d_w8 (out, 0xff);
		j3d_w16 (out, 0xffff);
	}
	for (int k = 0; k < 2; k++)
		if (has_c[k])
		{
			j3d_w32 (out, 11 + k);
			j3d_w32 (out, 1);
			j3d_w32 (out, 5);
			j3d_w8 (out, 0);
			j3d_w8 (out, 0xff);
			j3d_w16 (out, 0xffff);
		}
	for (int k = 0; k < 8; k++)
		if (has_t[k])
		{
			j3d_w32 (out, 13 + k);
			j3d_w32 (out, 1);
			j3d_w32 (out, e->texf32[k] ? 4 : 3);
			j3d_w8 (out, e->texf32[k] ? 0 : 8);
			j3d_w8 (out, 0xff);
			j3d_w16 (out, 0xffff);
		}
	j3d_w32 (out, 255);
	j3d_w32 (out, 1);
	j3d_w32 (out, 0);
	j3d_w8 (out, 0);
	j3d_w8 (out, 0xff);
	j3d_w16 (out, 0xffff);
	j3d_wpad (out, 32, 0);
	// data blobs in Vtx1OffsetIndex order
	uint32_t rel[13];
	memset (rel, 0, sizeof (rel));
#define J3D_VTX_OFF(slot)                                                                          \
	do                                                                                             \
	{                                                                                              \
		rel[slot] = (uint32_t)(out->size - start);                                                 \
		j3d_patch32 (out, offpos + (size_t)(slot) * 4, rel[slot]);                                 \
	} while (0)
	J3D_VTX_OFF (0);
	for (int i = 0; i < e->npos; i++)
	{
		j3d_wf32 (out, e->pos[i * 3]);
		j3d_wf32 (out, e->pos[i * 3 + 1]);
		j3d_wf32 (out, e->pos[i * 3 + 2]);
	}
	if (has_nrm)
	{
		J3D_VTX_OFF (1);
		for (int i = 0; i < e->nnrm; i++)
			for (int k = 0; k < 3; k++)
			{
				float v = e->nrm[i * 3 + k] * (float)(1 << 14);
				if (v > 32767)
					v = 32767;
				if (v < -32768)
					v = -32768;
				j3d_ws16 (out, (int)(v > 0 ? v + 0.5f : v - 0.5f));
			}
	}
	for (int k = 0; k < 2; k++)
		if (has_c[k])
		{
			J3D_VTX_OFF (3 + k);
			for (int i = 0; i < e->ncol[k]; i++)
				for (int c = 0; c < 4; c++)
				{
					float v = e->col[k][i * 4 + c] * 255.0f;
					if (v < 0)
						v = 0;
					if (v > 255)
						v = 255;
					j3d_w8 (out, (uint)(v + 0.5f));
				}
		}
	for (int k = 0; k < 8; k++)
		if (has_t[k])
		{
			J3D_VTX_OFF (5 + k);
			for (int i = 0; i < e->ntexp[k]; i++)
				for (int c = 0; c < 2; c++)
				{
					if (e->texf32[k])
						j3d_wf32 (out, e->texp[k][i * 2 + c]);
					else
					{
						float v = e->texp[k][i * 2 + c] * 256.0f;
						if (v > 32767)
							v = 32767;
						if (v < -32768)
							v = -32768;
						j3d_ws16 (out, (int)(v > 0 ? v + 0.5f : v - 0.5f));
					}
				}
		}
#undef J3D_VTX_OFF
	j3d_wpad (out, 32, 0);
	j3d_patch32 (out, start + 4, (uint32_t)(out->size - start));
}

static void j3d_write_evp1 (j3d_enc_t *e, j3d_buf_t *out)
{
	size_t start = out->size;
	j3d_wbytes (out, "EVP1", 4);
	j3d_w32 (out, 0);
	j3d_ws16 (out, e->nmultis);
	j3d_ws16 (out, -1);
	// NOTE: the per-joint inverse-bind table is always written, even with
	// zero envelopes: single-weight verts are baked against it, so dropping
	// it (like SuperBMD's empty section) loses skinning and --rotate.
	j3d_w32 (out, 28);
	j3d_w32 (out, 28 + (uint32_t)e->nmultis);
	size_t woff_pos = out->size;
	j3d_w32 (out, 0);
	size_t moff_pos = out->size;
	j3d_w32 (out, 0);
	for (int i = 0; i < e->nmultis; i++)
		j3d_w8 (out, (uint)e->multis[i].n);
	for (int i = 0; i < e->nmultis; i++)
		for (int k = 0; k < e->multis[i].n; k++)
			j3d_ws16 (out, e->multis[i].bones[k]);
	j3d_wpad (out, 4, 0);
	j3d_patch32 (out, woff_pos, (uint32_t)(out->size - start));
	for (int i = 0; i < e->nmultis; i++)
		for (int k = 0; k < e->multis[i].n; k++)
			j3d_wf32 (out, e->multis[i].weights[k]);
	j3d_patch32 (out, moff_pos, (uint32_t)(out->size - start));
	for (int j = 0; j < e->njoints; j++)
		for (int k = 0; k < 12; k++)
			j3d_wf32 (out, e->jibm[(size_t)j * 12 + k]);
	j3d_wpad (out, 32, 0);
	j3d_patch32 (out, start + 4, (uint32_t)(out->size - start));
}

static void j3d_write_drw1 (j3d_enc_t *e, j3d_buf_t *out)
{
	int nmulti2 = e->nmultis * 2;
	int count = e->nsingles + nmulti2;
	size_t start = out->size;
	j3d_wbytes (out, "DRW1", 4);
	j3d_w32 (out, 0);
	j3d_ws16 (out, count);
	j3d_ws16 (out, -1);
	j3d_w32 (out, 20);
	j3d_w32 (out, 20 + (uint32_t)count + (uint32_t)(count & 1));
	for (int i = 0; i < e->nsingles; i++)
		j3d_w8 (out, 0);
	for (int i = 0; i < nmulti2; i++)
		j3d_w8 (out, 1);
	j3d_wpad (out, 2, 0);
	for (int i = 0; i < e->nsingles; i++)
		j3d_ws16 (out, e->singles[i].bones[0]);
	for (int r = 0; r < 2; r++)
		for (int i = 0; i < e->nmultis; i++)
			j3d_ws16 (out, i); // EVP index
	j3d_wpad (out, 32, 0);
	j3d_patch32 (out, start + 4, (uint32_t)(out->size - start));
}

static void j3d_write_jnt1 (j3d_enc_t *e, j3d_buf_t *out)
{
	float gmn[3], gmx[3];
	gmn[0] = gmn[1] = gmn[2] = 1e30f;
	gmx[0] = gmx[1] = gmx[2] = -1e30f;
	for (int i = 0; i < e->npos; i++)
		for (int k = 0; k < 3; k++)
		{
			if (e->pos[i * 3 + k] < gmn[k])
				gmn[k] = e->pos[i * 3 + k];
			if (e->pos[i * 3 + k] > gmx[k])
				gmx[k] = e->pos[i * 3 + k];
		}
	if (gmn[0] > gmx[0])
		gmn[0] = gmn[1] = gmn[2] = gmx[0] = gmx[1] = gmx[2] = 0;
	float grad = j3d_bound_radius (gmn, gmx);
	size_t start = out->size;
	j3d_wbytes (out, "JNT1", 4);
	j3d_w32 (out, 0);
	j3d_ws16 (out, e->njoints);
	j3d_ws16 (out, -1);
	j3d_w32 (out, 24);
	size_t roff_pos = out->size;
	j3d_w32 (out, 0);
	size_t noff_pos = out->size;
	j3d_w32 (out, 0);
	for (int j = 0; j < e->njoints; j++)
	{
		j3d_ws16 (out, e->jmtx[j]);
		j3d_w8 (out, 0);
		j3d_w8 (out, 0xff);
		j3d_wf32 (out, e->jtrs[(size_t)j * 9]);
		j3d_wf32 (out, e->jtrs[(size_t)j * 9 + 1]);
		j3d_wf32 (out, e->jtrs[(size_t)j * 9 + 2]);
		for (int k = 0; k < 3; k++)
		{
			float deg = e->jtrs[(size_t)j * 9 + 3 + k];
			float v = deg * 32768.0f / 180.0f;
			if (v > 32767)
				v = 32767;
			if (v < -32768)
				v = -32768;
			j3d_ws16 (out, (int)(v > 0 ? v + 0.5f : v - 0.5f));
		}
		j3d_ws16 (out, -1);
		j3d_wf32 (out, e->jtrs[(size_t)j * 9 + 6]);
		j3d_wf32 (out, e->jtrs[(size_t)j * 9 + 7]);
		j3d_wf32 (out, e->jtrs[(size_t)j * 9 + 8]);
		j3d_wf32 (out, grad);
		j3d_wf32 (out, gmn[0]);
		j3d_wf32 (out, gmn[1]);
		j3d_wf32 (out, gmn[2]);
		j3d_wf32 (out, gmx[0]);
		j3d_wf32 (out, gmx[1]);
		j3d_wf32 (out, gmx[2]);
	}
	j3d_patch32 (out, roff_pos, (uint32_t)(out->size - start));
	for (int j = 0; j < e->njoints; j++)
		j3d_ws16 (out, j);
	j3d_wpad (out, 4, 0);
	j3d_patch32 (out, noff_pos, (uint32_t)(out->size - start));
	j3d_write_nametable (out, e->jnames, e->njoints);
	j3d_wpad (out, 32, 0);
	j3d_patch32 (out, start + 4, (uint32_t)(out->size - start));
}

// descriptor signature for dedup
typedef struct
{
	int attrs[24]; // attr ids in canonical order
	int n;
} j3d_sig_t;
static void j3d_mesh_sig (j3d_enc_t *e, int mi, j3d_sig_t *s)
{
	j3d_emesh_t *em = &e->em[mi];
	s->n = 0;
	if (j3d_mesh_has_pmtx (e, mi))
		s->attrs[s->n++] = 0;
	s->attrs[s->n++] = 9;
	if (e->nnrm > 0)
	{
		int any = 0;
		for (int i = 0; i < em->nverts; i++)
			if (em->ev[i].hasn)
				any = 1;
		if (any)
			s->attrs[s->n++] = 10;
	}
	for (int k = 0; k < 2; k++)
		if (e->ncol[k] > 0)
		{
			int any = 0;
			for (int i = 0; i < em->nverts; i++)
				if (em->ev[i].hasc[k])
					any = 1;
			if (any)
				s->attrs[s->n++] = 11 + k;
		}
	for (int k = 0; k < 8; k++)
		if (e->ntexp[k] > 0)
		{
			int any = 0;
			for (int i = 0; i < em->nverts; i++)
				if (em->ev[i].hast[k])
					any = 1;
			if (any)
				s->attrs[s->n++] = 13 + k;
		}
}
static int j3d_sig_same (const j3d_sig_t *a, const j3d_sig_t *b)
{
	if (a->n != b->n)
		return 0;
	for (int i = 0; i < a->n; i++)
		if (a->attrs[i] != b->attrs[i])
			return 0;
	return 1;
}

static void j3d_write_shp1 (j3d_enc_t *e, j3d_buf_t *out)
{
	// unique descriptors (first pass: assign, second pass below writes)
	j3d_sig_t *sigs = MALLOC ((size_t)(e->nem ? e->nem : 1) * sizeof (*sigs));
	int *sig_of = MALLOC ((size_t)(e->nem ? e->nem : 1) * sizeof (int));
	int nsigs = 0;
	for (int mi = 0; mi < e->nem && sigs && sig_of; mi++)
	{
		j3d_sig_t s;
		j3d_mesh_sig (e, mi, &s);
		int f = -1;
		for (int k = 0; k < nsigs; k++)
			if (j3d_sig_same (&sigs[k], &s))
				f = k;
		if (f < 0)
		{
			f = nsigs++;
			sigs[f] = s;
		}
		sig_of[mi] = f;
	}
	// NOTE: descriptors are written per unique sig below; sig offsets:
	size_t start = out->size;
	j3d_wbytes (out, "SHP1", 4);
	j3d_w32 (out, 0);
	j3d_ws16 (out, e->nem);
	j3d_ws16 (out, -1);
	size_t hdr_pos[8];
	for (int i = 0; i < 8; i++)
	{
		hdr_pos[i] = out->size;
		j3d_w32 (out, 0);
	}
	// shape headers (patched later for attr/mtx/pkt)
	size_t *shape_recs = MALLOC ((size_t)(e->nem ? e->nem : 1) * sizeof (size_t));
	for (int mi = 0; mi < e->nem; mi++)
	{
		if (shape_recs)
			shape_recs[mi] = out->size;
		float mn[3], mx[3];
		j3d_enc_bounds (e, mi, mn, mx);
		j3d_w8 (out, e->em[mi].billboard == 1 ? 1 : e->em[mi].billboard == 2 ? 2 : 3);
		j3d_w8 (out, 0xff);
		j3d_ws16 (out, e->em[mi].npkts);
		size_t ap = out->size;
		j3d_ws16 (out, 0); // attr off (patched)
		size_t mp = out->size;
		j3d_ws16 (out, 0); // mtx idx (patched)
		size_t fp = out->size;
		j3d_ws16 (out, 0); // first pkt (patched)
		j3d_ws16 (out, 0xffff);
		j3d_wf32 (out, j3d_bound_radius (mn, mx));
		j3d_wf32 (out, mn[0]);
		j3d_wf32 (out, mn[1]);
		j3d_wf32 (out, mn[2]);
		j3d_wf32 (out, mx[0]);
		j3d_wf32 (out, mx[1]);
		j3d_wf32 (out, mx[2]);
		(void)ap;
		(void)mp;
		(void)fp;
	}
	// remap identity
	j3d_patch32 (out, hdr_pos[1], (uint32_t)(out->size - start));
	for (int mi = 0; mi < e->nem; mi++)
		j3d_ws16 (out, mi);
	j3d_wpad (out, 32, 0);
	// attribute descriptors
	j3d_patch32 (out, hdr_pos[3], (uint32_t)(out->size - start));
	size_t desc_base = out->size;
	int *sig_off = MALLOC ((size_t)(nsigs ? nsigs : 1) * sizeof (int));
	for (int k = 0; k < nsigs; k++)
	{
		if (sig_off)
			sig_off[k] = (int)(out->size - desc_base);
		for (int a = 0; a < sigs[k].n; a++)
		{
			j3d_w32 (out, (uint32_t)sigs[k].attrs[a]);
			j3d_w32 (out, sigs[k].attrs[a] == 0 ? 1 : 3); // Direct pmtx, Index16 rest
		}
		j3d_w32 (out, 255);
		j3d_w32 (out, 0);
	}
	// packet matrix indices
	j3d_patch32 (out, hdr_pos[4], (uint32_t)(out->size - start));
	int total_pkts = 0;
	for (int mi = 0; mi < e->nem; mi++)
		total_pkts += e->em[mi].npkts;
	int *pkt_mtx_start = MALLOC ((size_t)(total_pkts ? total_pkts : 1) * sizeof (int));
	int *pkt_mtx_cnt = MALLOC ((size_t)(total_pkts ? total_pkts : 1) * sizeof (int));
	int gpk = 0, mtx_counter = 0;
	for (int mi = 0; mi < e->nem; mi++)
		for (int pi = 0; pi < e->em[mi].npkts; pi++)
		{
			j3d_epkt_t *pk = &e->em[mi].pkts[pi];
			if (pkt_mtx_start)
				pkt_mtx_start[gpk] = mtx_counter;
			if (pkt_mtx_cnt)
				pkt_mtx_cnt[gpk] = pk->nslots;
			for (int k = 0; k < pk->nslots; k++)
				j3d_ws16 (out, pk->slots[k]);
			mtx_counter += pk->nslots;
			gpk++;
		}
	j3d_wpad (out, 32, 0);
	// primitives
	j3d_patch32 (out, hdr_pos[5], (uint32_t)(out->size - start));
	size_t prim_base = out->size;
	int *pkt_psize = MALLOC ((size_t)(total_pkts ? total_pkts : 1) * sizeof (int));
	int *pkt_poff = MALLOC ((size_t)(total_pkts ? total_pkts : 1) * sizeof (int));
	gpk = 0;
	for (int mi = 0; mi < e->nem; mi++)
	{
		j3d_sig_t s;
		j3d_mesh_sig (e, mi, &s);
		int has_pmtx = s.n && s.attrs[0] == 0;
		for (int pi = 0; pi < e->em[mi].npkts; pi++)
		{
			j3d_epkt_t *pk = &e->em[mi].pkts[pi];
			if (pkt_poff)
				pkt_poff[gpk] = (int)(out->size - prim_base);
			int done = 0;
			while (done < pk->ntris)
			{
				int chunk = pk->ntris - done;
				if (chunk > 21845)
					chunk = 21845;
				j3d_w8 (out, 0x90);
				j3d_ws16 (out, chunk * 3);
				for (int i = 0; i < chunk * 3; i++)
				{
					int ci = done * 3 + i;
					if (has_pmtx)
						j3d_w8 (out, (uint)(pk->local[ci] * 3));
					j3d_ws16 (out, pk->rp[ci]);
					for (int a = 0; a < s.n; a++)
					{
						int at = s.attrs[a];
						if (at == 0 || at == 9)
							continue;
						int idx = 0;
						if (at == 10)
							idx = pk->rn[ci] < 0 ? 0 : pk->rn[ci];
						else if (at == 11)
							idx = pk->rc0[ci] < 0 ? 0 : pk->rc0[ci];
						else if (at == 12)
							idx = pk->rc1[ci] < 0 ? 0 : pk->rc1[ci];
						else
							idx = pk->rt[at - 13][ci] < 0 ? 0 : pk->rt[at - 13][ci];
						j3d_ws16 (out, idx);
					}
				}
				done += chunk;
			}
			if (pkt_psize)
				pkt_psize[gpk] = (int)(out->size - prim_base - (size_t)pkt_poff[gpk]);
			j3d_wpad (out, 32, 0);
			gpk++;
		}
	}
	// matrix data
	j3d_patch32 (out, hdr_pos[6], (uint32_t)(out->size - start));
	for (int i = 0; i < total_pkts; i++)
	{
		j3d_ws16 (out, 0);
		j3d_ws16 (out, pkt_mtx_cnt ? pkt_mtx_cnt[i] : 0);
		j3d_w32 (out, (uint32_t)(pkt_mtx_start ? pkt_mtx_start[i] : 0));
	}
	// packet info
	j3d_patch32 (out, hdr_pos[7], (uint32_t)(out->size - start));
	for (int i = 0; i < total_pkts; i++)
	{
		j3d_w32 (out, (uint32_t)(pkt_psize ? pkt_psize[i] : 0));
		j3d_w32 (out, (uint32_t)(pkt_poff ? pkt_poff[i] : 0));
	}
	j3d_wpad (out, 32, 0);
	// shape header offset + backpatch records
	j3d_patch32 (out, hdr_pos[0], 44);
	gpk = 0;
	for (int mi = 0; mi < e->nem; mi++)
	{
		size_t rec = shape_recs ? shape_recs[mi] : 0;
		// attr off
		out->data[rec + 4] = (uint8_t)((sig_off ? sig_off[sig_of[mi]] : 0) >> 8);
		out->data[rec + 5] = (uint8_t)(sig_off ? sig_off[sig_of[mi]] : 0);
		// mtx idx + first pkt
		out->data[rec + 6] = (uint8_t)(gpk >> 8);
		out->data[rec + 7] = (uint8_t)gpk;
		out->data[rec + 8] = (uint8_t)(gpk >> 8);
		out->data[rec + 9] = (uint8_t)gpk;
		gpk += e->em[mi].npkts;
	}
	j3d_patch32 (out, start + 4, (uint32_t)(out->size - start));
	FREE (sigs);
	FREE (sig_of);
	FREE (sig_off);
	FREE (shape_recs);
	FREE (pkt_mtx_start);
	FREE (pkt_mtx_cnt);
	FREE (pkt_psize);
	FREE (pkt_poff);
}

// per-material resolved bindings for MAT3
typedef struct
{
	int cull; // 0..3
	uint8_t diffuse[4];
	int tex[8]; // TEX1 indices, -1
	int stage_kind; // 0 tex, 1 texras, 2 ras, 3 white
	int textured;
} j3d_emat_t;

static const uint8_t j3d_chan_default[8] = { 0, 0, 0, 2, 1, 0, 0xff, 0xff };
static const uint8_t j3d_tevord_tex[4] = { 0, 0, 4, 0xff };
static const uint8_t j3d_tevord_ras[4] = { 0xff, 0xff, 4, 0xff };
static const uint8_t j3d_stage_tex[20]
	= { 0xff, 8, 15, 15, 15, 0, 0, 0, 1, 0, 4, 7, 7, 7, 0, 0, 0, 1, 0, 0xff };
static const uint8_t j3d_stage_texras[20]
	= { 0xff, 8, 15, 10, 15, 0, 0, 0, 1, 0, 4, 7, 5, 7, 0, 0, 0, 1, 0, 0xff };
static const uint8_t j3d_stage_ras[20]
	= { 0xff, 15, 15, 15, 10, 0, 0, 0, 1, 0, 7, 7, 7, 5, 0, 0, 0, 1, 0, 0xff };
static const uint8_t j3d_stage_white[20]
	= { 0xff, 15, 15, 15, 12, 0, 0, 0, 1, 0, 7, 7, 7, 6, 0, 0, 0, 1, 0, 0xff };

static void j3d_write_indirect_default (j3d_buf_t *out)
{
	j3d_w8 (out, 0);
	j3d_w8 (out, 0);
	j3d_w16 (out, 0xffff);
	for (int i = 0; i < 4; i++)
	{
		j3d_w8 (out, 0xff);
		j3d_w8 (out, 0xff);
		j3d_w16 (out, 0xffff);
	}
	for (int i = 0; i < 3; i++)
	{
		j3d_wf32 (out, 0.5f);
		j3d_wf32 (out, 0.0f);
		j3d_wf32 (out, 0.0f);
		j3d_wf32 (out, 0.0f);
		j3d_wf32 (out, 0.5f);
		j3d_wf32 (out, 0.0f);
		j3d_w8 (out, 1);
		j3d_w8 (out, 0xff);
		j3d_w16 (out, 0xffff);
	}
	for (int i = 0; i < 4; i++)
	{
		j3d_w8 (out, 0);
		j3d_w8 (out, 0);
		j3d_w16 (out, 0xffff);
	}
	for (int i = 0; i < 16; i++)
	{
		if (i < 3)
		{
			uint8_t b[12] = { 0, 0, 1, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff };
			j3d_wbytes (out, b, 12);
		}
		else
		{
			uint8_t b[12] = { 0 };
			j3d_wbytes (out, b, 12);
		}
	}
}
static void j3d_write_texmtx_identity (j3d_buf_t *out)
{
	j3d_w8 (out, 0);
	j3d_w8 (out, 0);
	j3d_w16 (out, 0xffff);
	j3d_wf32 (out, 0);
	j3d_wf32 (out, 0);
	j3d_wf32 (out, 0);
	j3d_wf32 (out, 1);
	j3d_wf32 (out, 1);
	j3d_ws16 (out, 0);
	j3d_w16 (out, 0xffff);
	j3d_wf32 (out, 0);
	j3d_wf32 (out, 0);
	for (int i = 0; i < 16; i++)
		j3d_wf32 (out, i % 5 == 0 ? 1.0f : 0.0f);
}

static void j3d_write_mat3 (j3d_enc_t *e, j3d_emat_t *emat, int nmats, j3d_buf_t *out)
{
	// unique block entries
	int culls[4], nculls = 0;
	uint8_t mcols[64][4];
	int nmcols = 0;
	int tremap[64], ntremap = 0;
	int tevord_used[2] = { 0, 0 }; // 0 tex, 1 ras
	int stage_used[4] = { 0, 0, 0, 0 };
	int tgc_used[2] = { 0, 0 };
	for (int m = 0; m < nmats; m++)
	{
		int f = 0;
		for (int k = 0; k < nculls; k++)
			if (culls[k] == emat[m].cull)
				f = 1;
		if (!f && nculls < 4)
			culls[nculls++] = emat[m].cull;
		f = 0;
		for (int k = 0; k < nmcols; k++)
			if (!memcmp (mcols[k], emat[m].diffuse, 4))
				f = 1;
		if (!f && nmcols < 64)
		{
			memcpy (mcols[nmcols], emat[m].diffuse, 4);
			nmcols++;
		}
		for (int k = 0; k < 8; k++)
		{
			if (emat[m].tex[k] < 0)
				continue;
			f = 0;
			for (int q = 0; q < ntremap; q++)
				if (tremap[q] == emat[m].tex[k])
					f = 1;
			if (!f && ntremap < 64)
				tremap[ntremap++] = emat[m].tex[k];
		}
		if (emat[m].textured)
		{
			tevord_used[0] = 1;
			tgc_used[1] = 1;
		}
		else
		{
			tevord_used[1] = 1;
			tgc_used[0] = 1;
		}
		stage_used[emat[m].stage_kind] = 1;
	}
	size_t start = out->size;
	j3d_wbytes (out, "MAT3", 4);
	j3d_w32 (out, 0);
	j3d_ws16 (out, nmats);
	j3d_ws16 (out, -1);
	size_t slotpos[30];
	slotpos[0] = out->size;
	j3d_w32 (out, 132); // MaterialData offset, always 132
	for (int i = 1; i < 30; i++)
	{
		slotpos[i] = out->size;
		j3d_w32 (out, 0);
	}
	// init records
	for (int m = 0; m < nmats; m++)
	{
		int cullidx = 0, mcolidx = 0;
		for (int k = 0; k < nculls; k++)
			if (culls[k] == emat[m].cull)
				cullidx = k;
		for (int k = 0; k < nmcols; k++)
			if (!memcmp (mcols[k], emat[m].diffuse, 4))
				mcolidx = k;
		int tgc = emat[m].textured ? 1 : 0, tgcidx = 0;
		{
			int k = 0;
			for (; k < 2; k++)
				if (tgc_used[k] == tgc || (k == 1 && !tgc_used[0]) || (k == 0 && !tgc_used[1]))
					break;
			// map value->position in emitted list
			tgcidx = 0;
			int pos = 0;
			for (int v = 0; v < 2; v++)
				if (tgc_used[v])
				{
					if (v == tgc)
						tgcidx = pos;
					pos++;
				}
			(void)k;
		}
		j3d_w8 (out, 1); // flag
		j3d_w8 (out, (uint)cullidx);
		j3d_w8 (out, 0); // chan count idx (only {2})
		j3d_w8 (out, (uint)tgcidx);
		j3d_w8 (out, 0); // tev stage count idx (only {1})
		j3d_w8 (out, 0); // zcomploc
		j3d_w8 (out, 0); // zmode
		j3d_w8 (out, 0); // dither
		j3d_ws16 (out, mcolidx);
		j3d_ws16 (out, -1);
		j3d_ws16 (out, 0);
		j3d_ws16 (out, 1);
		j3d_ws16 (out, -1);
		j3d_ws16 (out, -1);
		j3d_ws16 (out, 0); // ambient gray
		j3d_ws16 (out, -1);
		for (int k = 0; k < 8; k++)
			j3d_ws16 (out, -1); // lights
		for (int k = 0; k < 8; k++)
			j3d_ws16 (out, (emat[m].textured && k == 0) ? 0 : -1); // tg1
		for (int k = 0; k < 8; k++)
			j3d_ws16 (out, -1); // tg2
		for (int k = 0; k < 10; k++)
			j3d_ws16 (out, (emat[m].textured && k == 0) ? 0 : -1); // tm1
		for (int k = 0; k < 20; k++)
			j3d_ws16 (out, -1); // tm2
		for (int k = 0; k < 8; k++)
		{
			int ri = -1;
			if (emat[m].tex[k] >= 0)
				for (int q = 0; q < ntremap; q++)
					if (tremap[q] == emat[m].tex[k])
						ri = q;
			j3d_ws16 (out, ri);
		}
		j3d_ws16 (out, 0); // konst white
		for (int k = 1; k < 4; k++)
			j3d_ws16 (out, -1);
		for (int k = 0; k < 16; k++)
			j3d_w8 (out, 0); // color sels
		for (int k = 0; k < 16; k++)
			j3d_w8 (out, 0); // alpha sels
		{
			int ord = emat[m].textured ? 0 : 1, oidx = 0, pos = 0;
			for (int v = 0; v < 2; v++)
				if (tevord_used[v])
				{
					if (v == ord)
						oidx = pos;
					pos++;
				}
			for (int k = 0; k < 16; k++)
				j3d_ws16 (out, k == 0 ? oidx : -1);
		}
		j3d_ws16 (out, 0); // tev color white
		for (int k = 1; k < 4; k++)
			j3d_ws16 (out, -1);
		{
			int sidx = 0, pos = 0;
			for (int v = 0; v < 4; v++)
				if (stage_used[v])
				{
					if (v == emat[m].stage_kind)
						sidx = pos;
					pos++;
				}
			for (int k = 0; k < 16; k++)
				j3d_ws16 (out, k == 0 ? sidx : -1);
		}
		for (int k = 0; k < 16; k++)
			j3d_ws16 (out, k == 0 ? 0 : -1); // swap
		for (int k = 0; k < 16; k++)
			j3d_ws16 (out, k == 0 ? 0 : -1); // swaptab
		j3d_ws16 (out, 0); // fog
		j3d_ws16 (out, 0); // alpha
		j3d_ws16 (out, 0); // blend
		j3d_ws16 (out, 0); // nbt
	}
#define J3D_MAT_SLOT(i) j3d_patch32 (out, slotpos[i], (uint32_t)(out->size - start))
	J3D_MAT_SLOT (1);
	for (int m = 0; m < nmats; m++)
	{
		// remap identity over unique materials (no dedup: 1:1)
		j3d_ws16 (out, m);
	}
	J3D_MAT_SLOT (2);
	{
		char (*mn)[64] = MALLOC ((size_t)(nmats ? nmats : 1) * 64);
		for (int m = 0; m < nmats; m++)
			if ((size_t)m < e->model->num_materials)
				snprintf (mn[m], 64, "%s", e->model->materials[m].name);
			else
				snprintf (mn[m], 64, "material");
		j3d_write_nametable (out, mn, nmats);
		FREE (mn);
	}
	j3d_wpad (out, 8, 0);
	J3D_MAT_SLOT (3);
	j3d_write_indirect_default (out);
	J3D_MAT_SLOT (4);
	for (int k = 0; k < nculls; k++)
		j3d_w32 (out, (uint32_t)culls[k]);
	J3D_MAT_SLOT (5);
	for (int k = 0; k < nmcols; k++)
		j3d_wbytes (out, mcols[k], 4);
	J3D_MAT_SLOT (6);
	j3d_w8 (out, 2);
	j3d_wpad (out, 4, 0);
	J3D_MAT_SLOT (7);
	j3d_wbytes (out, j3d_chan_default, 8);
	j3d_wbytes (out, j3d_chan_default, 8);
	J3D_MAT_SLOT (8);
	{
		uint8_t g[4] = { 50, 50, 50, 50 };
		j3d_wbytes (out, g, 4);
	}
	J3D_MAT_SLOT (9); // light: empty
	J3D_MAT_SLOT (10);
	if (tgc_used[0])
		j3d_w8 (out, 0);
	if (tgc_used[1])
		j3d_w8 (out, 1);
	j3d_wpad (out, 4, 0);
	J3D_MAT_SLOT (11);
	{
		uint8_t g[4] = { 1, 4, 60, 0xff };
		j3d_wbytes (out, g, 4);
	}
	J3D_MAT_SLOT (12); // texgen2: empty
	J3D_MAT_SLOT (13);
	j3d_write_texmtx_identity (out);
	J3D_MAT_SLOT (14); // texmtx2: empty
	J3D_MAT_SLOT (15);
	for (int k = 0; k < ntremap; k++)
		j3d_ws16 (out, tremap[k]);
	j3d_wpad (out, 4, 0);
	J3D_MAT_SLOT (16);
	if (tevord_used[0])
		j3d_wbytes (out, j3d_tevord_tex, 4);
	if (tevord_used[1])
		j3d_wbytes (out, j3d_tevord_ras, 4);
	J3D_MAT_SLOT (17);
	j3d_ws16 (out, 0x00ff);
	j3d_ws16 (out, 0x00ff);
	j3d_ws16 (out, 0x00ff);
	j3d_ws16 (out, 0x00ff);
	J3D_MAT_SLOT (18);
	{
		uint8_t w[4] = { 0xff, 0xff, 0xff, 0xff };
		j3d_wbytes (out, w, 4);
	}
	J3D_MAT_SLOT (19);
	j3d_w8 (out, 1);
	j3d_wpad (out, 4, 0);
	J3D_MAT_SLOT (20);
	if (stage_used[0])
		j3d_wbytes (out, j3d_stage_tex, 20);
	if (stage_used[1])
		j3d_wbytes (out, j3d_stage_texras, 20);
	if (stage_used[2])
		j3d_wbytes (out, j3d_stage_ras, 20);
	if (stage_used[3])
		j3d_wbytes (out, j3d_stage_white, 20);
	J3D_MAT_SLOT (21);
	{
		uint8_t s[4] = { 0, 0, 0xff, 0xff };
		j3d_wbytes (out, s, 4);
	}
	J3D_MAT_SLOT (22);
	{
		uint8_t s[4] = { 0, 1, 2, 3 };
		j3d_wbytes (out, s, 4);
	}
	J3D_MAT_SLOT (23);
	{
		j3d_w8 (out, 0);
		j3d_w8 (out, 0);
		j3d_w16 (out, 0);
		for (int k = 0; k < 4; k++)
			j3d_wf32 (out, 0);
		for (int k = 0; k < 4; k++)
			j3d_w8 (out, 0);
		for (int k = 0; k < 10; k++)
			j3d_w16 (out, 0);
	}
	J3D_MAT_SLOT (24);
	{
		uint8_t a[8] = { 4, 0x7f, 0, 7, 0, 0xff, 0xff, 0xff };
		j3d_wbytes (out, a, 8);
	}
	J3D_MAT_SLOT (25);
	{
		uint8_t b[4] = { 1, 4, 5, 5 };
		j3d_wbytes (out, b, 4);
	}
	J3D_MAT_SLOT (26);
	{
		uint8_t z[4] = { 1, 3, 1, 0xff };
		j3d_wbytes (out, z, 4);
	}
	J3D_MAT_SLOT (27);
	j3d_w8 (out, 0);
	j3d_wpad (out, 4, 0);
	J3D_MAT_SLOT (28);
	j3d_w8 (out, 0);
	j3d_wpad (out, 4, 0);
	J3D_MAT_SLOT (29);
	j3d_w8 (out, 0);
	j3d_w8 (out, 0xff);
	j3d_w16 (out, 0xffff);
	j3d_wf32 (out, 0);
	j3d_wf32 (out, 0);
	j3d_wf32 (out, 0);
#undef J3D_MAT_SLOT
	j3d_wpad (out, 32, 0);
	j3d_patch32 (out, start + 4, (uint32_t)(out->size - start));
}

static void j3d_write_mdl3_stub (int nmats, j3d_buf_t *out)
{
	// Structural MDL3 with empty per-material blobs. Games execute MDL3
	// display lists for BDL rendering, so this stub keeps the file
	// parseable but NOT fully in-game functional; prefer BMD output.
	size_t start = out->size;
	j3d_wbytes (out, "MDL3", 4);
	j3d_w32 (out, 0);
	j3d_ws16 (out, nmats);
	j3d_ws16 (out, -1);
	j3d_w32 (out, 0x40);
	size_t s2 = out->size;
	j3d_w32 (out, 0);
	size_t s3 = out->size;
	j3d_w32 (out, 0);
	size_t s4 = out->size;
	j3d_w32 (out, 0);
	size_t s5 = out->size;
	j3d_w32 (out, 0);
	size_t st = out->size;
	j3d_w32 (out, 0);
	j3d_wpad (out, 32, 0);
	size_t cmdtab = out->size; // == start+0x40
	for (int i = 0; i < nmats; i++)
	{
		j3d_w32 (out, (uint32_t)(nmats * 8)); // all blobs empty at table end
		j3d_w32 (out, 0);
	}
	j3d_patch32 (out, s2, (uint32_t)(out->size - start));
	for (int i = 0; i < nmats; i++)
		for (int k = 0; k < 4; k++)
			j3d_w32 (out, 0);
	j3d_patch32 (out, s3, (uint32_t)(out->size - start));
	for (int i = 0; i < nmats; i++)
		for (int k = 0; k < 2; k++)
			j3d_w32 (out, 0);
	j3d_patch32 (out, s4, (uint32_t)(out->size - start));
	for (int i = 0; i < nmats; i++)
		j3d_w8 (out, 1);
	j3d_wpad (out, 4, 0);
	j3d_patch32 (out, s5, (uint32_t)(out->size - start));
	for (int i = 0; i < nmats; i++)
		j3d_ws16 (out, i);
	j3d_wpad (out, 4, 0);
	j3d_patch32 (out, st, (uint32_t)(out->size - start));
	j3d_ws16 (out, 0);
	j3d_wpad (out, 32, 0);
	j3d_patch32 (out, start + 4, (uint32_t)(out->size - start));
	(void)cmdtab;
}

static void j3d_write_tex1 (j3d_enc_t *e, j3d_buf_t *out)
{
	size_t start = out->size;
	j3d_wbytes (out, "TEX1", 4);
	j3d_w32 (out, 0);
	j3d_ws16 (out, e->ntex);
	j3d_ws16 (out, -1);
	j3d_w32 (out, 32);
	size_t ntab_pos = out->size;
	j3d_w32 (out, 0);
	j3d_wpad (out, 32, 0);
	// headers (patched later)
	size_t *hoffs = MALLOC ((size_t)(e->ntex ? e->ntex : 1) * sizeof (size_t));
	for (int i = 0; i < e->ntex; i++)
	{
		j3d_etex_t *t = &e->tex[i];
		if (hoffs)
			hoffs[i] = out->size;
		j3d_w8 (out, (uint)t->fmt);
		j3d_w8 (out, 0); // alpha setting
		j3d_w16 (out, t->w);
		j3d_w16 (out, t->h);
		j3d_w8 (out, t->wrap_s);
		j3d_w8 (out, t->wrap_t);
		j3d_w8 (out, 0); // palettes off
		j3d_w8 (out, 0);
		j3d_w16 (out, 0);
		j3d_w32 (out, 0); // pal off
		j3d_w8 (out, 0); // mip
		j3d_w8 (out, 0); // edgeLOD
		j3d_w8 (out, 0); // biasClamp
		j3d_w8 (out, 0); // maxAniso
		j3d_w8 (out, t->minf);
		j3d_w8 (out, t->magf);
		j3d_w8 (out, 0); // minLOD
		j3d_w8 (out, (uint)(t->nmips * 8 > 127 ? 127 : t->nmips * 8)); // maxLOD
		j3d_w8 (out, (uint)(t->nmips + 1)); // image count
		j3d_w8 (out, 0xff);
		j3d_ws16 (out, 0); // lodBias
		j3d_w32 (out, 0); // image off (patched)
	}
	// image data
	for (int i = 0; i < e->ntex; i++)
	{
		j3d_etex_t *t = &e->tex[i];
		size_t hrel = hoffs ? hoffs[i] - start : 0;
		j3d_patch32 (out, hoffs[i] + 28, (uint32_t)((out->size - start) - hrel));
		j3d_tex_encode (out, t->rgba, t->w, t->h, t->fmt);
		for (int m = 0; m < t->nmips; m++)
			j3d_tex_encode (out, t->mip_rgba[m], t->mip_w[m], t->mip_h[m], t->fmt);
		j3d_wpad (out, 32, 0);
	}
	FREE (hoffs);
	j3d_patch32 (out, ntab_pos, (uint32_t)(out->size - start));
	{
		char (*tn)[64] = MALLOC ((size_t)(e->ntex ? e->ntex : 1) * 64);
		for (int i = 0; i < e->ntex && tn; i++)
			snprintf (tn[i], 64, "%s", e->tex[i].name);
		j3d_write_nametable (out, tn, e->ntex);
		FREE (tn);
	}
	j3d_wpad (out, 32, 0);
	j3d_patch32 (out, start + 4, (uint32_t)(out->size - start));
}

static void j3d_enc_free (j3d_enc_t *e)
{
	FREE (e->jnames);
	FREE (e->jtrs);
	FREE (e->jparent);
	FREE (e->jmtx);
	FREE (e->jibm);
	FREE (e->singles);
	FREE (e->multis);
	for (int i = 0; i < e->ntex; i++)
	{
		FREE (e->tex[i].rgba);
		for (int m = 0; m < e->tex[i].nmips; m++)
			FREE (e->tex[i].mip_rgba[m]);
		FREE (e->tex[i].mip_rgba);
		FREE (e->tex[i].mip_w);
		FREE (e->tex[i].mip_h);
	}
	FREE (e->tex);
	for (int i = 0; i < e->nem; i++)
	{
		FREE (e->em[i].ev);
		for (int p = 0; p < e->em[i].npkts; p++)
		{
			FREE (e->em[i].pkts[p].slots);
			FREE (e->em[i].pkts[p].local);
			FREE (e->em[i].pkts[p].rp);
			FREE (e->em[i].pkts[p].rn);
			FREE (e->em[i].pkts[p].rc0);
			FREE (e->em[i].pkts[p].rc1);
			for (int k = 0; k < 8; k++)
				FREE (e->em[i].pkts[p].rt[k]);
		}
		FREE (e->em[i].pkts);
	}
	FREE (e->em);
	FREE (e->pos);
	FREE (e->nrm);
	FREE (e->col[0]);
	FREE (e->col[1]);
	FREE (e->posmap.keys);
	FREE (e->posmap.vals);
	FREE (e->nrmmap.keys);
	FREE (e->nrmmap.vals);
	for (int k = 0; k < 2; k++)
	{
		FREE (e->colmap[k].keys);
		FREE (e->colmap[k].vals);
	}
	for (int k = 0; k < 8; k++)
	{
		FREE (e->texp[k]);
		FREE (e->texmap[k].keys);
		FREE (e->texmap[k].vals);
	}
	FREE (e->mat_diffuse);
	FREE (e->mat_texstr);
	FREE (e->mat_cull);
	FREE (e->mat_texnames);
}

enumError EncodeModelToJ3D (const model_t *model, const char *out_path, const j3d_encode_opt_t *opt)
{
	j3d_encode_opt_t def;
	if (!opt)
	{
		SetupDefaultJ3DEncodeOpt (&def);
		opt = &def;
	}
	if (!model || !model->num_meshes || !out_path)
		return ERROR0 (ERR_INVALID_DATA, "j3d: nothing to encode\n");
	j3d_enc_t e;
	memset (&e, 0, sizeof (e));
	e.model = model;
	e.opt = opt;
	if (opt->tex_dir && *opt->tex_dir)
		snprintf (e.input_dir, sizeof (e.input_dir), "%s", opt->tex_dir);
	else
		snprintf (e.input_dir, sizeof (e.input_dir), ".");
	// joints
	e.njoints = (int)model->num_joints;
	if (!e.njoints)
		e.njoints = 1; // synthetic root
	e.jnames = CALLOC ((size_t)e.njoints, 64);
	e.jtrs = CALLOC ((size_t)e.njoints * 9, sizeof (float));
	e.jparent = CALLOC ((size_t)e.njoints, sizeof (int));
	e.jmtx = CALLOC ((size_t)e.njoints, sizeof (int));
	e.jibm = CALLOC ((size_t)e.njoints * 12, sizeof (float));
	if (!e.jnames || !e.jtrs || !e.jparent || !e.jmtx || !e.jibm)
	{
		j3d_enc_free (&e);
		return ERROR0 (ERR_OUT_OF_MEMORY, "j3d: out of memory\n");
	}
	if (!model->num_joints)
	{
		snprintf (e.jnames[0], 64, "root");
		e.jtrs[0] = e.jtrs[1] = e.jtrs[2] = 1.0f;
		e.jparent[0] = -1;
		e.jmtx[0] = 0;
		for (int k = 0; k < 12; k++)
			e.jibm[k] = (k % 5 == 0) ? 1.0f : 0.0f; // identity 3x4 (diag idx 0,5,10)
	}
	else
		for (int j = 0; j < e.njoints; j++)
		{
			const joint_t *jn = &model->joints[j];
			snprintf (e.jnames[j], 64, "%s", jn->name[0] ? jn->name : "joint");
			e.jtrs[j * 9] = jn->scale.x ? jn->scale.x : 1.0f;
			e.jtrs[j * 9 + 1] = jn->scale.y ? jn->scale.y : 1.0f;
			e.jtrs[j * 9 + 2] = jn->scale.z ? jn->scale.z : 1.0f;
			// NOTE: a zero scale component is almost certainly a missing
			// value from a foreign importer, not an intentional flatten;
			// guard against singular joints here.
			e.jtrs[j * 9 + 3] = jn->rotate.x;
			e.jtrs[j * 9 + 4] = jn->rotate.y;
			e.jtrs[j * 9 + 5] = jn->rotate.z;
			e.jtrs[j * 9 + 6] = jn->translate.x;
			e.jtrs[j * 9 + 7] = jn->translate.y;
			e.jtrs[j * 9 + 8] = jn->translate.z;
			e.jparent[j] = jn->parent_idx;
			e.jmtx[j] = 1;
			if (jn->has_inverse_bind)
				memcpy (e.jibm + (size_t)j * 12, jn->inverse_bind, 12 * sizeof (float));
			else
			{
				for (int k = 0; k < 12; k++)
					e.jibm[(size_t)j * 12 + k] = (k % 5 == 0) ? 1.0f : 0.0f;
			}
			if (opt->rotate_model)
				j3d_rotate_ibm (e.jibm + (size_t)j * 12, e.jibm + (size_t)j * 12);
		}
	// materials: JSON overrides first (need names only)
	int nmats = model->num_materials ? (int)model->num_materials : 1;
	if (opt->mat_path && *opt->mat_path)
		j3d_apply_materials_json (&e, opt->mat_path, nmats);
	// resolve texture names per material, collect
	char (*mtexnames)[8][64] = CALLOC ((size_t)nmats, sizeof (*mtexnames));
	int *mtexidx = CALLOC ((size_t)nmats * 8, sizeof (int));
	if (!mtexnames || !mtexidx)
	{
		FREE (mtexnames);
		FREE (mtexidx);
		j3d_enc_free (&e);
		return ERROR0 (ERR_OUT_OF_MEMORY, "j3d: out of memory\n");
	}
	for (int m = 0; m < nmats; m++)
		for (int k = 0; k < 8; k++)
		{
			mtexidx[m * 8 + k] = -1;
			mtexnames[m][k][0] = 0;
			const char *nm = 0;
			if (e.mat_texnames && e.mat_texnames[m][k] == -1)
				continue; // JSON nulled
			if (e.mat_texnames && e.mat_texnames[m][k] == -3)
				nm = e.mat_texstr[m][k];
			else if ((size_t)m < model->num_materials && k < model->materials[m].num_textures)
				nm = model->materials[m].textures[k];
			if (!nm || !*nm)
				continue;
			snprintf (mtexnames[m][k], 64, "%s", nm);
			int ti = j3d_collect_texture (&e, nm);
			if (ti >= 0)
				mtexidx[m * 8 + k] = ti;
		}
	if (opt->texheader_path && *opt->texheader_path)
		j3d_apply_texheaders (&e, opt->texheader_path);
	// preserve embedded-but-unreferenced images (e.g. extra material
	// layers the GLB material binding dropped) as unbound TEX1 entries
	for (size_t ii = 0; ii < model->num_images; ii++)
	{
		const model_image_t *im = &model->images[ii];
		int known = 0;
		for (int k = 0; k < e.ntex; k++)
			if (!strcmp (e.tex[k].name, im->name))
				known = 1;
		if (!known && im->name[0])
		{
			char base[64];
			snprintf (base, sizeof (base), "%s", im->name);
			char *dot = strrchr (base, '.');
			if (dot)
				*dot = 0;
			known = 0;
			for (int k = 0; k < e.ntex; k++)
				if (!strcmp (e.tex[k].name, base) || !strcmp (e.tex[k].name, im->name))
					known = 1;
			if (!known)
				j3d_collect_texture (&e, base[0] ? base : im->name);
		}
	}
	// meshes
	e.nem = (int)model->num_meshes;
	e.em = CALLOC ((size_t)e.nem, sizeof (*e.em));
	if (!e.em)
	{
		FREE (mtexnames);
		FREE (mtexidx);
		j3d_enc_free (&e);
		return ERROR0 (ERR_OUT_OF_MEMORY, "j3d: out of memory\n");
	}
	for (int mi = 0; mi < e.nem; mi++)
		if (!j3d_expand_mesh (&e, mi))
		{
			FREE (e.em[mi].ev);
			e.em[mi].ev = 0;
			e.em[mi].nverts = 0;
		}
	j3d_collect_weights (&e);
	// UV float decisions
	for (int k = 0; k < 8; k++)
	{
		e.texf32[k] = opt->tex_float32 ? 1 : 0;
		if (!e.texf32[k])
			for (int mi = 0; mi < e.nem && !e.texf32[k]; mi++)
				for (int i = 0; i < e.em[mi].nverts; i++)
					if (e.em[mi].ev[i].hast[k]
						&& (e.em[mi].ev[i].uv[k][0] < -128.0f || e.em[mi].ev[i].uv[k][0] > 127.99f
							|| e.em[mi].ev[i].uv[k][1] < -128.0f
							|| e.em[mi].ev[i].uv[k][1] > 127.99f))
						e.texf32[k] = 1;
	}
	for (int mi = 0; mi < e.nem; mi++)
		if (e.em[mi].nverts && !j3d_build_packets (&e, mi))
		{
			FREE (mtexnames);
			FREE (mtexidx);
			j3d_enc_free (&e);
			return ERROR0 (ERR_OUT_OF_MEMORY, "j3d: out of memory\n");
		}
	if (!e.npos)
	{
		FREE (mtexnames);
		FREE (mtexidx);
		j3d_enc_free (&e);
		return ERROR0 (ERR_INVALID_DATA, "j3d: no geometry\n");
	}
	// resolved per-material bindings
	j3d_emat_t *emat = CALLOC ((size_t)nmats, sizeof (*emat));
	if (!emat)
	{
		FREE (mtexnames);
		FREE (mtexidx);
		j3d_enc_free (&e);
		return ERROR0 (ERR_OUT_OF_MEMORY, "j3d: out of memory\n");
	}
	for (int m = 0; m < nmats; m++)
	{
		emat[m].cull = 2;
		emat[m].diffuse[0] = emat[m].diffuse[1] = emat[m].diffuse[2] = 255;
		emat[m].diffuse[3] = 255;
		for (int k = 0; k < 8; k++)
			emat[m].tex[k] = -1;
		if ((size_t)m < model->num_materials)
		{
			const material_t *sm = &model->materials[m];
			if (e.mat_cull && e.mat_cull[m] != 0xff)
				emat[m].cull = e.mat_cull[m];
			float *dd = (e.mat_diffuse && e.mat_diffuse[m * 4] >= 0) ? e.mat_diffuse + m * 4
																	 : (float *)sm->diffuse;
			int allzero = dd[0] == 0 && dd[1] == 0 && dd[2] == 0 && dd[3] == 0;
			for (int k = 0; k < 4; k++)
			{
				float v = allzero ? 1.0f : dd[k];
				if (v < 0)
					v = 0;
				if (v > 1)
					v = 1;
				emat[m].diffuse[k] = (uint8_t)(v * 255 + 0.5f);
			}
		}
		else if (e.mat_cull && e.mat_cull[m] != 0xff)
			emat[m].cull = e.mat_cull[m];
		if (e.mat_diffuse && e.mat_diffuse[m * 4] >= 0 && (size_t)m >= model->num_materials)
			for (int k = 0; k < 4; k++)
			{
				float v = e.mat_diffuse[m * 4 + k];
				if (v < 0)
					v = 0;
				if (v > 1)
					v = 1;
				emat[m].diffuse[k] = (uint8_t)(v * 255 + 0.5f);
			}
		for (int k = 0; k < 8; k++)
			emat[m].tex[k] = mtexidx[m * 8 + k];
		emat[m].textured = 0;
		for (int k = 0; k < 8; k++)
			if (emat[m].tex[k] >= 0)
				emat[m].textured = 1;
		emat[m].stage_kind = 3;
		if (emat[m].textured)
		{
			int vc = 0;
			for (int mi = 0; mi < e.nem; mi++)
				if (e.em[mi].mat == m && e.em[mi].hasvcol)
					vc = 1;
			emat[m].stage_kind = vc ? 1 : 0;
		}
		else
		{
			int vc = 0;
			for (int mi = 0; mi < e.nem; mi++)
				if (e.em[mi].mat == m && e.em[mi].hasvcol)
					vc = 1;
			emat[m].stage_kind = vc ? 2 : 3;
		}
	}
	FREE (mtexnames);
	FREE (mtexidx);
	// remap mesh material -1 -> 0 when default material exists
	for (int mi = 0; mi < e.nem; mi++)
		if (e.em[mi].mat < 0)
			e.em[mi].mat = 0;
	// sections
	j3d_buf_t inf1, vtx1, evp1, drw1, jnt1, shp1, mat3, mdl3, tex1;
	memset (&inf1, 0, sizeof (inf1));
	memset (&vtx1, 0, sizeof (vtx1));
	memset (&evp1, 0, sizeof (evp1));
	memset (&drw1, 0, sizeof (drw1));
	memset (&jnt1, 0, sizeof (jnt1));
	memset (&shp1, 0, sizeof (shp1));
	memset (&mat3, 0, sizeof (mat3));
	memset (&mdl3, 0, sizeof (mdl3));
	memset (&tex1, 0, sizeof (tex1));
	int npk = 0;
	j3d_write_inf1 (&e, &inf1, &npk);
	j3d_write_vtx1 (&e, &vtx1);
	j3d_write_evp1 (&e, &evp1);
	j3d_write_drw1 (&e, &drw1);
	j3d_write_jnt1 (&e, &jnt1);
	j3d_write_shp1 (&e, &shp1);
	j3d_write_mat3 (&e, emat, nmats, &mat3);
	if (opt->is_bdl)
		j3d_write_mdl3_stub (nmats, &mdl3);
	j3d_write_tex1 (&e, &tex1);
	FREE (emat);
	// file
	j3d_buf_t f;
	memset (&f, 0, sizeof (f));
	j3d_wbytes (&f, opt->is_bdl ? "J3D2bdl4" : "J3D2bmd3", 8);
	j3d_w32 (&f, 0);
	j3d_w32 (&f, opt->is_bdl ? 9 : 8);
	{
		char ver[16];
		memset (ver, 0, sizeof (ver));
		memcpy (ver, "NintoolboxJ3D1.0", 16);
		j3d_wbytes (&f, ver, 16);
	}
	j3d_wbytes (&f, inf1.data, inf1.size);
	j3d_wbytes (&f, vtx1.data, vtx1.size);
	j3d_wbytes (&f, evp1.data, evp1.size);
	j3d_wbytes (&f, drw1.data, drw1.size);
	j3d_wbytes (&f, jnt1.data, jnt1.size);
	j3d_wbytes (&f, shp1.data, shp1.size);
	j3d_wbytes (&f, mat3.data, mat3.size);
	if (opt->is_bdl)
		j3d_wbytes (&f, mdl3.data, mdl3.size);
	j3d_wbytes (&f, tex1.data, tex1.size);
	j3d_patch32 (&f, 8, (uint32_t)f.size);
	enumError err = SaveFILE (out_path, 0, true, f.data, (uint)f.size, 0);
	if (err != ERR_OK)
		fprintf (stderr, "j3d: cannot write %s\n", out_path);
	if (opt->is_bdl)
		fprintf (stderr,
			"j3d: warning: BDL MDL3 display lists are stubbed (parseable but render "
			"nothing); prefer BMD output for in-game use\n");
	j3d_free_buf (&f);
	j3d_free_buf (&inf1);
	j3d_free_buf (&vtx1);
	j3d_free_buf (&evp1);
	j3d_free_buf (&drw1);
	j3d_free_buf (&jnt1);
	j3d_free_buf (&shp1);
	j3d_free_buf (&mat3);
	j3d_free_buf (&mdl3);
	j3d_free_buf (&tex1);
	j3d_enc_free (&e);
	return err;
}
