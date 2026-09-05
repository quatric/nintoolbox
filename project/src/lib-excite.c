// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-excite.h"
#include "lib-model-glb.h"
#include "lib-image.h"
#include <math.h>

static inline u16 xrd_le16 (const u8 *p)
{
	return (u16)p[1] << 8 | p[0];
}
static inline u16 xrd_be16 (const u8 *p)
{
	return (u16)p[0] << 8 | p[1];
}
static inline void xwr_be16 (u8 *p, u16 v)
{
	p[0] = (u8)(v >> 8);
	p[1] = (u8)v;
}
static inline void xwr_le16 (u8 *p, u16 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

// Sanity cap on encoder output size, matching lib-nintendo.c's NFMT_MAX_OUTPUT.
#define XTEX_MAX_OUTPUT (512u << 20)

//-----------------------------------------------------------------------------
///////////////		.tex / .art GX texture recovery		///////////////
//-----------------------------------------------------------------------------
//
// Ported from ExciteExtract's classify_tex.py + gxtex.py (Python ground
// truth; see the Excite RE notes for the discovery process). Both games
// store raw GX (GameCube/Wii) texture data but never record the pixel
// FORMAT anywhere in the file -- the retail engine looked it up from the
// material instead. This file recovers it by brute-force decoding every
// plausible GX format and scoring which one produces a coherent image:
//
//   .tex: mip level 1 (when present) must be a correct 2x box-downsample of
//         level 0 for the true format, and looks like noise for every wrong
//         one ("mip_score").
//   .art/.img: no mip chain exists, so instead the tiled layout of GX pixel
//         data is exploited -- at the true width, pixel deltas measured
//         *across* 4/8-pixel tile boundaries are no larger than deltas
//         measured *inside* a tile; at a wrong width the tiles land in the
//         wrong place and boundary deltas spike ("seam_score").
//
// Neither heuristic is exact, so this is inherently best-effort: on some
// files it can pick a plausible-but-wrong format. That matches the ~2% of
// files ExciteExtract itself could not resolve.

typedef struct gxfmt_t
{
	u8 id;
	u8 bpp; // bits per pixel
	u8 bw, bh; // tile block size
} gxfmt_t;

// Excludes paletted formats (C4/C8/C14X2): these files carry no TLUT, so a
// palette can never be recovered and the candidate is never worth trying.
#define GX_I4 0x0
#define GX_I8 0x1
#define GX_IA4 0x2
#define GX_IA8 0x3
#define GX_RGB565 0x4
#define GX_RGB5A3 0x5
#define GX_RGBA32 0x6
#define GX_CMPR 0xE

static const gxfmt_t gx_formats[] = {
	{ GX_I4, 4, 8, 8 },
	{ GX_I8, 8, 8, 4 },
	{ GX_IA4, 8, 8, 4 },
	{ GX_IA8, 16, 4, 4 },
	{ GX_RGB565, 16, 4, 4 },
	{ GX_RGB5A3, 16, 4, 4 },
	{ GX_RGBA32, 32, 4, 4 },
	{ GX_CMPR, 4, 8, 8 },
};
#define N_GX_FORMATS (sizeof (gx_formats) / sizeof (gx_formats[0]))

// Candidate try order copied from classify_tex.py's CAND tuple.
static const u8 gx_candidates[]
	= { GX_CMPR, GX_I8, GX_IA4, GX_IA8, GX_RGB5A3, GX_RGB565, GX_I4, GX_RGBA32 };
#define N_GX_CANDIDATES (sizeof (gx_candidates) / sizeof (gx_candidates[0]))

static const uint gx_dims[] = { 4, 8, 16, 32, 64, 128, 256, 512, 1024 };
#define N_GX_DIMS (sizeof (gx_dims) / sizeof (gx_dims[0]))

static const gxfmt_t *find_gx_format (u8 id)
{
	for (uint i = 0; i < N_GX_FORMATS; i++)
		if (gx_formats[i].id == id)
			return gx_formats + i;
	return 0;
}

static uint gx_level_size (u8 fmt, uint w, uint h)
{
	const gxfmt_t *f = find_gx_format (fmt);
	if (!f)
		return 0;
	const uint tw = (w + f->bw - 1) / f->bw * f->bw;
	const uint th = (h + f->bh - 1) / f->bh * f->bh;
	return tw * th * f->bpp / 8;
}

static u64 gx_chain_size (u8 fmt, uint w, uint h, uint levels)
{
	u64 tot = 0;
	for (uint i = 0; i < levels; i++)
	{
		const uint lw = w >> i ? w >> i : 1;
		const uint lh = h >> i ? h >> i : 1;
		tot += gx_level_size (fmt, lw, lh);
	}
	return tot;
}

static inline void rgb565_to_rgba (u16 v, u8 *out)
{
	const uint r = (v >> 11) & 0x1f, g = (v >> 5) & 0x3f, b = v & 0x1f;
	out[0] = r * 255 / 31;
	out[1] = g * 255 / 63;
	out[2] = b * 255 / 31;
	out[3] = 255;
}

static inline void rgb5a3_to_rgba (u16 v, u8 *out)
{
	if (v & 0x8000)
	{
		const uint r = (v >> 10) & 0x1f, g = (v >> 5) & 0x1f, b = v & 0x1f;
		out[0] = r * 255 / 31;
		out[1] = g * 255 / 31;
		out[2] = b * 255 / 31;
		out[3] = 255;
	}
	else
	{
		const uint a = (v >> 12) & 0x7, r = (v >> 8) & 0xf, g = (v >> 4) & 0xf, b = v & 0xf;
		out[0] = r * 17;
		out[1] = g * 17;
		out[2] = b * 17;
		out[3] = a * 255 / 7;
	}
}

// Decode 'fmt' GX pixel data (big-endian, as stored on disc) to a freshly
// MALLOC'd width*height*4 RGBA8 buffer. Truncated source data just leaves
// the remainder of the buffer at its zero-initialised default, matching
// gxtex.py's early-return-on-underrun behaviour.
static u8 *gx_decode (u8 fmt, uint w, uint h, const u8 *data, uint size)
{
	const gxfmt_t *gf = find_gx_format (fmt);
	if (!gf)
		return 0;
	u8 *out = CALLOC (1, (size_t)w * h * 4);
	if (!out)
		return 0;

#define PUT(x, y, r, g, b, a)                                                                      \
	do                                                                                             \
	{                                                                                              \
		if ((uint)(x) < w && (uint)(y) < h)                                                        \
		{                                                                                          \
			u8 *o = out + ((size_t)(y) * w + (x)) * 4;                                             \
			o[0] = (r);                                                                            \
			o[1] = (g);                                                                            \
			o[2] = (b);                                                                            \
			o[3] = (a);                                                                            \
		}                                                                                          \
	} while (0)

	uint p = 0;
	for (uint by = 0; by < h; by += gf->bh)
		for (uint bx = 0; bx < w; bx += gf->bw)
		{
			switch (fmt)
			{
				case GX_I4:
					for (uint y = 0; y < 8; y++)
						for (uint x = 0; x < 8; x += 2)
						{
							if (p >= size)
								goto done;
							const u8 b = data[p++];
							const uint hi = b >> 4, lo = b & 0xf;
							PUT (bx + x, by + y, hi * 17, hi * 17, hi * 17, 255);
							PUT (bx + x + 1, by + y, lo * 17, lo * 17, lo * 17, 255);
						}
					break;

				case GX_I8:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 8; x++)
						{
							if (p >= size)
								goto done;
							const u8 v = data[p++];
							PUT (bx + x, by + y, v, v, v, 255);
						}
					break;

				case GX_IA4:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 8; x++)
						{
							if (p >= size)
								goto done;
							const u8 b = data[p++];
							const uint i = (b & 0xf) * 17, a = (b >> 4) * 17;
							PUT (bx + x, by + y, i, i, i, a);
						}
					break;

				case GX_IA8:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
						{
							if (p + 1 >= size)
								goto done;
							const u16 v = xrd_be16 (data + p);
							p += 2;
							const uint a = v >> 8, i = v & 0xff;
							PUT (bx + x, by + y, i, i, i, a);
						}
					break;

				case GX_RGB565:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
						{
							if (p + 1 >= size)
								goto done;
							u8 px[4];
							rgb565_to_rgba (xrd_be16 (data + p), px);
							p += 2;
							PUT (bx + x, by + y, px[0], px[1], px[2], px[3]);
						}
					break;

				case GX_RGB5A3:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
						{
							if (p + 1 >= size)
								goto done;
							u8 px[4];
							rgb5a3_to_rgba (xrd_be16 (data + p), px);
							p += 2;
							PUT (bx + x, by + y, px[0], px[1], px[2], px[3]);
						}
					break;

				case GX_RGBA32:
				{
					u8 a_[16], r_[16];
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
						{
							if (p + 1 >= size)
								goto done;
							a_[y * 4 + x] = data[p];
							r_[y * 4 + x] = data[p + 1];
							p += 2;
						}
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
						{
							if (p + 1 >= size)
								goto done;
							const u8 g = data[p], b = data[p + 1];
							p += 2;
							PUT (bx + x, by + y, r_[y * 4 + x], g, b, a_[y * 4 + x]);
						}
				}
				break;

				case GX_CMPR:
					for (uint sy = 0; sy < 8; sy += 4)
						for (uint sx = 0; sx < 8; sx += 4)
						{
							if (p + 7 >= size)
								goto done;
							const u16 c0 = xrd_be16 (data + p), c1 = xrd_be16 (data + p + 2);
							const u32 bits = (u32)data[p + 4] << 24 | (u32)data[p + 5] << 16
								| (u32)data[p + 6] << 8 | data[p + 7];
							p += 8;
							u8 col[4][4];
							rgb565_to_rgba (c0, col[0]);
							rgb565_to_rgba (c1, col[1]);
							if (c0 > c1)
							{
								for (int k = 0; k < 3; k++)
								{
									col[2][k] = (u8)((col[0][k] * 2 + col[1][k]) / 3);
									col[3][k] = (u8)((col[0][k] + col[1][k] * 2) / 3);
								}
								col[2][3] = col[3][3] = 255;
							}
							else
							{
								for (int k = 0; k < 3; k++)
									col[2][k] = (u8)((col[0][k] + col[1][k]) / 2);
								col[2][3] = 255;
								col[3][0] = col[3][1] = col[3][2] = col[3][3] = 0;
							}
							for (uint y = 0; y < 4; y++)
								for (uint x = 0; x < 4; x++)
								{
									const uint idx = (bits >> (30 - (y * 4 + x) * 2)) & 3;
									PUT (bx + sx + x, by + sy + y, col[idx][0], col[idx][1],
										col[idx][2], col[idx][3]);
								}
						}
					break;
			}
		}
done:
#undef PUT
	return out;
}

enumError DecodeGXTexture_RGBA (u8 **dest, uint w, uint h, uint fmt, const u8 *data, uint size,
	const u8 *palette, uint palette_count, uint pal_format)
{
	if (!dest || !w || !h || !data)
		return EINVAL;
	if (fmt <= 6 || fmt == 14)
	{
		u8 *rgba = gx_decode ((u8)fmt, w, h, data, size);
		if (!rgba)
			return EINVAL;
		*dest = rgba;
		return ERR_OK;
	}
	if ((fmt != 8 && fmt != 9) || !palette || !palette_count || pal_format > 2)
		return EINVAL;
	const uint bw = fmt == 8 ? 8 : 8, bh = fmt == 8 ? 8 : 4, bytes = fmt == 8 ? 32 : 32;
	const uint need = ((w + bw - 1) / bw) * ((h + bh - 1) / bh) * bytes;
	if (size < need)
		return EINVAL;
	u8 *rgba = CALLOC (1, (size_t)w * h * 4);
	if (!rgba)
		return ERR_CANT_CREATE;
	uint p = 0;
	for (uint by = 0; by < h; by += bh)
		for (uint bx = 0; bx < w; bx += bw)
			for (uint y = 0; y < bh; y++)
				for (uint x = 0; x < bw; x += (fmt == 8 ? 2 : 1))
				{
					uint idx;
					if (fmt == 8)
					{
						u8 v = data[p++];
						for (uint k = 0; k < 2; k++)
						{
							idx = k ? v & 15 : v >> 4;
							if (bx + x + k < w && by + y < h && idx < palette_count)
							{
								u8 *o = rgba + ((size_t)(by + y) * w + bx + x + k) * 4;
								u16 c = xrd_be16 (palette + 2 * idx);
								if (pal_format == 0)
								{
									o[3] = c >> 8;
									o[0] = o[1] = o[2] = c;
								}
								else if (pal_format == 1)
									rgb565_to_rgba (c, o);
								else
									rgb5a3_to_rgba (c, o);
							}
						}
					}
					else
					{
						idx = data[p++];
						if (bx + x < w && by + y < h && idx < palette_count)
						{
							u8 *o = rgba + ((size_t)(by + y) * w + bx + x) * 4;
							u16 c = xrd_be16 (palette + 2 * idx);
							if (pal_format == 0)
							{
								o[3] = c >> 8;
								o[0] = o[1] = o[2] = c;
							}
							else if (pal_format == 1)
								rgb565_to_rgba (c, o);
							else
								rgb5a3_to_rgba (c, o);
						}
					}
				}
	*dest = rgba;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		.tex / .art GX texture encoding			///////////////
//-----------------------------------------------------------------------------

static enumError scan_excite_header (excite_tex_t *tex, const u8 *data, uint size);
//
// gx_encode() is the exact byte-for-byte inverse of gx_decode() above: same
// tile traversal, same bit-packing, so anything it writes decodes back to
// the same pixels through the unmodified decoder. CMPR is the one format
// that isn't a simple per-pixel inverse (it's a block compressor); rather
// than reinvent one, it reuses this codebase's existing DXT1-style block
// encoder (CMPR_wiimm()/CMPR_close_info() in lib-image1.c, the same one
// wimgt's own CMPR conversion uses) -- its vector format (16 raster-order
// RGBA8 pixels per 4x4 block) matches our buffers exactly, no shuffling
// needed.

static inline u8 rgba_to_i (const u8 *p)
{
	return (u8)(((uint)p[0] + p[1] + p[2]) / 3);
}
static inline u8 to_nibble (u8 v)
{
	return (u8)(((uint)v * 15 + 127) / 255);
}

// Encode one w*h RGBA8 mip level to 'out' (caller-allocated, must be exactly
// gx_level_size(fmt,w,h) bytes). Source reads past 'w'/'h' (only possible on
// the smallest mip levels, where a format's tile is bigger than the level
// itself) are edge-clamped.
static void gx_encode (u8 fmt, uint w, uint h, const u8 *rgba, u8 *out)
{
	const gxfmt_t *gf = find_gx_format (fmt);
	if (!gf)
		return;

#define GETPX(x, y)                                                                                \
	(rgba + ((size_t)((uint)(y) < h ? (y) : h - 1) * w + ((uint)(x) < w ? (x) : w - 1)) * 4)

	uint p = 0;
	for (uint by = 0; by < h; by += gf->bh)
		for (uint bx = 0; bx < w; bx += gf->bw)
		{
			switch (fmt)
			{
				case GX_I4:
					for (uint y = 0; y < 8; y++)
						for (uint x = 0; x < 8; x += 2)
						{
							const u8 hi = to_nibble (rgba_to_i (GETPX (bx + x, by + y)));
							const u8 lo = to_nibble (rgba_to_i (GETPX (bx + x + 1, by + y)));
							out[p++] = (u8)(hi << 4 | lo);
						}
					break;

				case GX_I8:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 8; x++)
							out[p++] = rgba_to_i (GETPX (bx + x, by + y));
					break;

				case GX_IA4:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 8; x++)
						{
							const u8 *s = GETPX (bx + x, by + y);
							out[p++] = (u8)(to_nibble (s[3]) << 4 | to_nibble (rgba_to_i (s)));
						}
					break;

				case GX_IA8:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
						{
							const u8 *s = GETPX (bx + x, by + y);
							out[p] = s[3];
							out[p + 1] = rgba_to_i (s);
							p += 2;
						}
					break;

				case GX_RGB565:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
						{
							const u8 *s = GETPX (bx + x, by + y);
							const u16 v
								= (u16)(s[0] >> 3) << 11 | (u16)(s[1] >> 2) << 5 | (s[2] >> 3);
							xwr_be16 (out + p, v);
							p += 2;
						}
					break;

				case GX_RGB5A3:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
						{
							const u8 *s = GETPX (bx + x, by + y);
							u16 v;
							if (s[3] >= 224)
								v = 0x8000 | (u16)(s[0] >> 3) << 10 | (u16)(s[1] >> 3) << 5
									| (s[2] >> 3);
							else
								v = (u16)((s[3] * 7 + 127) / 255) << 12 | (u16)(s[0] >> 4) << 8
									| (u16)(s[1] >> 4) << 4 | (s[2] >> 4);
							xwr_be16 (out + p, v);
							p += 2;
						}
					break;

				case GX_RGBA32:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
						{
							const u8 *s = GETPX (bx + x, by + y);
							out[p] = s[3];
							out[p + 1] = s[0];
							p += 2; // a, r
						}
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
						{
							const u8 *s = GETPX (bx + x, by + y);
							out[p] = s[1];
							out[p + 1] = s[2];
							p += 2; // g, b
						}
					break;

				case GX_CMPR:
					for (uint sy = 0; sy < 8; sy += 4)
						for (uint sx = 0; sx < 8; sx += 4)
						{
							u8 vector[64];
							for (uint y = 0; y < 4; y++)
								for (uint x = 0; x < 4; x++)
									memcpy (vector + (y * 4 + x) * 4,
										GETPX (bx + sx + x, by + sy + y), 4);
							cmpr_info_t info;
							InitializeCmprInfo (&info);
							CMPR_wiimm (vector, &info);
							CMPR_close_info (vector, &info, out + p, false);
							p += 8;
						}
					break;
			}
		}
#undef GETPX
}

static bool is_gx_dim (uint v)
{
	for (uint i = 0; i < N_GX_DIMS; i++)
		if (gx_dims[i] == v)
			return true;
	return false;
}

// Box-downsample a sw*sh RGBA8 buffer to a freshly MALLOC'd dw*dh one
// (dw,dh each half of sw,sh, rounded down but never below 1) -- the same
// operation mip_score() assumes a real level1 is the result of.
static u8 *box_downsample2x (const u8 *src, uint sw, uint sh, uint dw, uint dh)
{
	u8 *out = MALLOC ((size_t)dw * dh * 4);
	if (!out)
		return 0;
	for (uint y = 0; y < dh; y++)
		for (uint x = 0; x < dw; x++)
		{
			const uint sx0 = x * 2, sy0 = y * 2;
			const uint sx1 = sx0 + 1 < sw ? sx0 + 1 : sx0;
			const uint sy1 = sy0 + 1 < sh ? sy0 + 1 : sy0;
			const u8 *p00 = src + ((size_t)sy0 * sw + sx0) * 4;
			const u8 *p10 = src + ((size_t)sy0 * sw + sx1) * 4;
			const u8 *p01 = src + ((size_t)sy1 * sw + sx0) * 4;
			const u8 *p11 = src + ((size_t)sy1 * sw + sx1) * 4;
			u8 *o = out + ((size_t)y * dw + x) * 4;
			for (int k = 0; k < 4; k++)
				o[k] = (u8)(((uint)p00[k] + p10[k] + p01[k] + p11[k] + 2) / 4);
		}
	return out;
}

// How many mip levels a full w*h chain gets, capped at 10 (classify_excite_
// tex()'s exact-chain-size search only tries lv=1..10, so anything wider
// would never be found again on decode).
static uint gx_mip_levels (uint w, uint h)
{
	uint levels = 1, cw = w, ch = h;
	while (levels < 10 && (cw > 1 || ch > 1))
	{
		cw = cw > 1 ? cw / 2 : 1;
		ch = ch > 1 ? ch / 2 : 1;
		levels++;
	}
	return levels;
}

// Scan an RGBA8 image once for the two content properties that drive format
// selection: whether every pixel is fully opaque, and whether every pixel is
// grey (r==g==b). Used to order candidate formats from most to least
// space-efficient; final correctness is always decided by self-verification
// (tex_self_verifies()/art_self_verifies()), not by this heuristic alone --
// RGB5A3/RGB565/IA8 all share the exact same bpp (16) and tile size (4x4),
// and I8/IA4 both share bpp 8 / tile 8x4, so classify_excite_tex()'s
// exact-byte-count candidate search can never tell either pair apart by size
// alone (confirmed empirically: a real ART image with binary alpha,
// round-tripped through RGB5A3, came back misclassified as RGB565).
static void rgba_content_flags (const u8 *rgba, uint w, uint h, bool *all_opaque, bool *is_gray)
{
	*all_opaque = true;
	*is_gray = true;
	const size_t n = (size_t)w * h;
	for (size_t i = 0; i < n; i++)
	{
		const u8 *p = rgba + i * 4;
		if (p[3] != 255)
			*all_opaque = false;
		if (p[0] != p[1] || p[1] != p[2])
			*is_gray = false;
		if (!*all_opaque && !*is_gray)
			break;
	}
}

static enumError encode_tex_once (
	u8 fmt, uint width, uint height, const u8 *rgba, u8 **out_dest, uint *out_size)
{
	const uint levels = gx_mip_levels (width, height);
	const u64 chain = gx_chain_size (fmt, width, height, levels);
	if (!chain || chain > XTEX_MAX_OUTPUT - 128)
		return ERR_INVALID_DATA;

	const uint total = (uint)chain + 128;
	u8 *out = CALLOC (1, total);
	if (!out)
		return ERR_CANT_CREATE;

	u8 *level = 0; // owned downsampled buffer for levels >0 (level0 is 'rgba', not owned)
	uint lw = width, lh = height;
	uint off = 0;
	for (uint i = 0; i < levels; i++)
	{
		const u8 *src = i ? level : rgba;
		gx_encode (fmt, lw, lh, src, out + off);
		off += gx_level_size (fmt, lw, lh);

		const uint nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1;
		if (i + 1 < levels)
		{
			u8 *next = box_downsample2x (src, lw, lh, nw, nh);
			FREE (level);
			level = next;
			if (!level)
			{
				FREE (out);
				return ERR_CANT_CREATE;
			}
		}
		lw = nw;
		lh = nh;
	}
	FREE (level);

	*out_dest = out;
	*out_size = total;
	return ERR_OK;
}

// Total byte count alone is ambiguous across many (format,width,height,
// level-count) combinations -- e.g. a 256x256 chain and a 128x512 chain can
// land on the exact same total (same pixel count, different level-count
// truncation), and classify_excite_tex()'s exact-size search will happily
// propose either. Matching width/height/format alone isn't quite enough
// either: a *different* format that happens to also recover the right
// width/height/format (via its own colliding sibling in the RGB5A3/RGB565/
// IA8 or I8/IA4 groups) can still decode to visibly wrong pixels (e.g. an
// intensity-only format silently discarding all chroma). So also require
// the classifier's decoded pixels to be close to what we actually encoded.
// Per-pixel max-channel abs diff, averaged over pixel count -- matches the
// metric used by the project's own PNG round-trip regression checks
// (t_exart_mask() et al.), not a flat per-byte average, since a single
// badly-off channel per pixel (e.g. quantized blue) matters more than
// diluting it across 4 channels that mostly match.
static bool rgba_close_enough (const u8 *a, const u8 *b, uint w, uint h)
{
	const size_t n = (size_t)w * h;
	double sum = 0;
	uint maxd = 0;
	for (size_t p = 0; p < n; p++)
	{
		uint d = 0;
		for (uint c = 0; c < 4; c++)
		{
			const int dc = abs ((int)a[p * 4 + c] - (int)b[p * 4 + c]);
			if ((uint)dc > d)
				d = dc;
		}
		if (d > maxd)
			maxd = d;
		sum += d;
	}
	return maxd <= 8 && sum / n <= 1.0;
}

// Self-verify a freshly-encoded payload by running it back through the real,
// unmodified classifier (ScanTEX()/ScanART()) and confirming it recovers the
// exact format/dimensions we intended, *and* that the decoded pixels are
// close to the ones we started from. Needed because neither format has a
// header: size and pixel format are both *guessed* from byte-count and
// content heuristics (see rgba_content_flags()'s comment on the RGB5A3/
// RGB565/IA8 and I8/IA4 collisions). For small images in particular, every mip
// level (or, for ART, the sole level) below a format's block size pads up to
// that format's minimum block size -- e.g. CMPR/I4's 8x8 block -- so many
// different (format,width,height,level-count) combinations can tie at the
// exact same total byte count. Blindly trusting one guess would silently
// ship files that decode back with different dimensions, format, or pixels;
// self-verifying and retrying with another format catches that.
static bool tex_self_verifies (
	const u8 *data, uint size, uint width, uint height, u8 fmt, const u8 *orig_rgba)
{
	excite_tex_t tex;
	if (ScanTEX (&tex, data, size))
		return false;
	bool ok = tex.width == width && tex.height == height && tex.gx_format == fmt;
	if (ok)
		ok = rgba_close_enough (orig_rgba, tex.rgba, width, height);
	ResetExciteTEX (&tex);
	return ok;
}

static bool art_self_verifies (
	const u8 *data, uint size, uint width, uint height, u8 fmt, const u8 *orig_rgba)
{
	excite_tex_t tex;
	if (ScanART (&tex, data, size))
		return false;
	bool ok = tex.width == width && tex.height == height && tex.gx_format == fmt;
	if (ok)
		ok = rgba_close_enough (orig_rgba, tex.rgba, width, height);
	ResetExciteTEX (&tex);
	return ok;
}

enumError EncodeExciteTEX_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, int gx_format)
{
	if (!dest || !dest_size)
		return EINVAL;
	*dest = 0;
	*dest_size = 0;
	if (!rgba || !width || !height || !is_gx_dim (width) || !is_gx_dim (height))
		return ERR_INVALID_DATA;

	// In auto-pick mode, try a short list of quality-ordered fallbacks if the
	// first guess doesn't survive a round trip through the real classifier
	// (both in dimensions/format recovered *and* in decoded pixel content --
	// see tex_self_verifies()). An explicit caller-requested format is
	// trusted as-is, same as before.
	u8 candidates[4];
	uint n_candidates = 0;
	if (gx_format >= 0)
		candidates[n_candidates++] = (u8)gx_format;
	else
	{
		bool all_opaque, is_gray;
		rgba_content_flags (rgba, width, height, &all_opaque, &is_gray);
		if (all_opaque)
		{
			if (is_gray)
				candidates[n_candidates++] = GX_I8;
			candidates[n_candidates++] = GX_CMPR;
		}
		candidates[n_candidates++] = GX_RGB5A3;
		candidates[n_candidates++] = GX_RGBA32;
	}

	for (uint c = 0; c < n_candidates; c++)
	{
		const u8 fmt = candidates[c];
		if (!find_gx_format (fmt))
			continue;

		u8 *out = 0;
		uint total = 0;
		if (encode_tex_once (fmt, width, height, rgba, &out, &total))
			continue;

		if (gx_format >= 0 || tex_self_verifies (out, total, width, height, fmt, rgba))
		{
			*dest = out;
			*dest_size = total;
			return ERR_OK;
		}
		FREE (out);
	}
	return ERR_INVALID_DATA;
}

enumError EncodeExciteART_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, int gx_format)
{
	if (!dest || !dest_size)
		return EINVAL;
	*dest = 0;
	*dest_size = 0;
	if (!rgba || !width || !height || !is_gx_dim (width) || !is_gx_dim (height))
		return ERR_INVALID_DATA;

	// ART has no mip chain, so classify_excite_tex() falls straight to the
	// seam-score/smoothness heuristics (see rgba_content_flags()'s comment)
	// -- RGBA32 doesn't share its bpp/tile signature with anything else in
	// gx_candidates, so it's tried first; the smaller lossy formats are only
	// tried as a fallback, and are only ever accepted if they also pass
	// art_self_verifies()'s pixel-closeness check.
	u8 candidates[3];
	uint n_candidates = 0;
	if (gx_format >= 0)
		candidates[n_candidates++] = (u8)gx_format;
	else
	{
		bool all_opaque, is_gray;
		rgba_content_flags (rgba, width, height, &all_opaque, &is_gray);
		candidates[n_candidates++] = GX_RGBA32;
		candidates[n_candidates++] = GX_RGB5A3;
		if (all_opaque)
			candidates[n_candidates++] = is_gray ? GX_I8 : GX_CMPR;
	}

	for (uint c = 0; c < n_candidates; c++)
	{
		const u8 fmt = candidates[c];
		if (!find_gx_format (fmt))
			continue;

		const uint level_size = gx_level_size (fmt, width, height);
		// ScanART() requires size-128 to be an exact power of two -- always
		// true here since 'width'/'height' are themselves powers of two and
		// every gx_formats[] bpp is also a power of two.
		if (!level_size || level_size > XTEX_MAX_OUTPUT - 128)
			continue;

		const uint total = level_size + 128;
		u8 *out = CALLOC (1, total); // trailing 128 bytes stay zero, as ScanART() requires
		if (!out)
			return ERR_CANT_CREATE;

		gx_encode (fmt, width, height, rgba, out);

		if (gx_format >= 0 || art_self_verifies (out, total, width, height, fmt, rgba))
		{
			*dest = out;
			*dest_size = total;
			return ERR_OK;
		}
		FREE (out);
	}
	return ERR_INVALID_DATA;
}

// Canonical renderer-state values observed in the ExciteBots retail corpus.
// The byte is not, in general, a pixel-format enum (several values select
// filtering/wrapping variants), but these choices are known-good defaults for
// newly-created resources. 0x40/0x41 are exceptional: the loader gives them
// I4/IA4 storage semantics and expects an additional 1024-byte renderer tail.
static uint excite_renderer_code (u8 fmt)
{
	switch (fmt)
	{
		case GX_I4: return 0x40;
		case GX_IA4: return 0x41;
		case GX_CMPR: return 0x42;
		case GX_RGBA32: return 0x47;
		case GX_I8: return 0x49;
		case GX_IA8:
		case GX_RGB565:
		case GX_RGB5A3: return 0x46;
		default: return 0;
	}
}

enumError EncodeExciteHeader_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width,
	uint height, int gx_format, uint levels, uint renderer_code)
{
	if (!dest || !dest_size)
		return EINVAL;
	*dest = 0;
	*dest_size = 0;
	if (!rgba || !is_gx_dim (width) || !is_gx_dim (height) || gx_format < 0
		|| !find_gx_format ((u8)gx_format) || levels < 1 || levels > 10)
		return ERR_INVALID_DATA;

	// Reject redundant levels after both dimensions have reached 1. Apart
	// from avoiding ambiguous files, this is the chain shape used by retail.
	uint max_levels = 1, lw = width, lh = height;
	while (max_levels < 10 && (lw > 1 || lh > 1))
	{
		lw = lw > 1 ? lw / 2 : 1;
		lh = lh > 1 ? lh / 2 : 1;
		max_levels++;
	}
	if (levels > max_levels)
		return ERR_INVALID_DATA;

	const u8 fmt = (u8)gx_format;
	if (!renderer_code)
		renderer_code = excite_renderer_code (fmt);
	if (renderer_code < 0x40 || renderer_code > 0x4f
		|| (renderer_code == 0x40 && fmt != GX_I4)
		|| (renderer_code == 0x41 && fmt != GX_IA4)
		|| (fmt == GX_I4 && renderer_code != 0x40)
		|| (fmt == GX_IA4 && renderer_code != 0x41))
		return ERR_INVALID_DATA;
	// The decoder has to preserve a retail quirk where (2,0x44) denotes one
	// level. Do not emit that ambiguous combination from the general encoder.
	if (renderer_code == 0x44 && levels == 2)
		return ERR_INVALID_DATA;

	const u64 chain = gx_chain_size (fmt, width, height, levels);
	const uint aux_size = renderer_code == 0x40 || renderer_code == 0x41 ? 1024 : 0;
	if (!chain || chain > XTEX_MAX_OUTPUT - 128 - aux_size)
		return ERR_INVALID_DATA;
	const uint total = 128 + (uint)chain + aux_size;
	u8 *out = CALLOC (1, total);
	if (!out)
		return ERR_CANT_CREATE;
	// Stable descriptor defaults used by the retail ExciteBots exporter.
	// These bytes include the ordinary wrap/filter state and descriptor-table
	// offset; leaving the whole descriptor zero happens to satisfy our scanner
	// but does not describe a usable texture to the game's renderer.
	out[6] = 3;
	out[7] = 3;
	out[8] = fmt == GX_I4 || fmt == GX_I8 || fmt == GX_CMPR ? 5 : 6;
	out[9] = 0x32;
	out[11] = 2;
	out[12] = 5;
	out[13] = 4;
	memset (out + 15, 0xe3, 5);
	out[20] = 0x18;
	out[28] = 0x68;
	if (renderer_code == 0x40 || renderer_code == 0x41)
	{
		// The auxiliary renderer record begins at 0x468 in the loaded object:
		// 0x68 is the on-disk descriptor offset and 0x400 its fixed tail size.
		out[24] = 0x68;
		out[29] = 4;
	}
	xwr_le16 (out, (u16)width);
	xwr_le16 (out + 2, (u16)height);
	out[4] = (u8)levels;
	out[5] = (u8)renderer_code;

	u8 *level = 0;
	lw = width;
	lh = height;
	uint off = 128;
	for (uint i = 0; i < levels; i++)
	{
		const u8 *src = i ? level : rgba;
		gx_encode (fmt, lw, lh, src, out + off);
		off += gx_level_size (fmt, lw, lh);
		if (i + 1 < levels)
		{
			const uint nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1;
			u8 *next = box_downsample2x (src, lw, lh, nw, nh);
			FREE (level);
			level = next;
			if (!level)
			{
				FREE (out);
				return ERR_CANT_CREATE;
			}
			lw = nw;
			lh = nh;
		}
	}
	FREE (level);

	// Exercise the production header parser, including its exact chain/tail
	// bounds checks. I4/IA4 are deterministic from the renderer code. Other
	// equal-byte-size formats remain material-selected in the game, so only
	// require the parser to recover dimensions for those formats.
	excite_tex_t verify;
	const enumError verr = scan_excite_header (&verify, out, total);
	const bool ok = !verr && verify.width == width && verify.height == height
		&& ((fmt != GX_I4 && fmt != GX_IA4) || verify.gx_format == fmt);
	ResetExciteTEX (&verify);
	if (!ok)
	{
		FREE (out);
		return ERR_INVALID_DATA;
	}
	*dest = out;
	*dest_size = total;
	return ERR_OK;
}

enumError EncodeExciteTEXHeader_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, int gx_format)
{
	return EncodeExciteHeader_RGBA (dest, dest_size, rgba, width, height,
		gx_format < 0 ? GX_RGBA32 : gx_format, gx_mip_levels (width, height), 0);
}

enumError EncodeExciteARTHeader_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, int gx_format)
{
	return EncodeExciteHeader_RGBA (
		dest, dest_size, rgba, width, height, gx_format < 0 ? GX_RGBA32 : gx_format, 1, 0);
}

// Distinct-value sample used to reject degenerate all-flat decodes (a wrong
// format can accidentally decode to a uniform colour, which trivially
// "passes" a naive smoothness test).
static uint count_distinct_sample (const u8 *rgba, uint n_px)
{
	uint seen = 0;
	const uint step = n_px / 4096 > 1 ? n_px / 4096 : 1;
	u32 first = 0;
	bool have_first = false;
	for (uint i = 0; i < n_px && seen < 3; i += step)
	{
		const u32 v = *(const u32 *)(rgba + (size_t)i * 4);
		if (!have_first)
		{
			first = v;
			have_first = true;
			seen = 1;
		}
		else if (v != first)
			seen++;
	}
	return seen;
}

// mip_score(): mean abs error between level0 box-downsampled 2x and the
// stored level1, sampled on a coarse grid (mirrors classify_tex.py exactly).
static double mip_score (const u8 *data, uint size, u8 fmt, uint w, uint h)
{
	if (w < 16 || h < 16)
		return -1;
	const uint L0 = gx_level_size (fmt, w, h);
	const uint L1 = gx_level_size (fmt, w / 2, h / 2);
	if (!L0 || !L1 || (u64)L0 + L1 > size)
		return -1;

	u8 *a = gx_decode (fmt, w, h, data, L0);
	u8 *c = gx_decode (fmt, w / 2, h / 2, data + L0, L1);
	if (!a || !c)
	{
		FREE (a);
		FREE (c);
		return -1;
	}

	if (count_distinct_sample (a, w * h) < 3 || count_distinct_sample (c, (w / 2) * (h / 2)) < 2)
	{
		FREE (a);
		FREE (c);
		return -1;
	}

	const uint hw = w / 2, hh = h / 2;
	u64 tot = 0;
	uint n = 0;
	const uint stepy = hh / 32 > 1 ? hh / 32 : 1;
	const uint stepx = hw / 32 > 1 ? hw / 32 : 1;
	for (uint y = 0; y < hh; y += stepy)
		for (uint x = 0; x < hw; x += stepx)
		{
			uint acc[4] = { 0, 0, 0, 0 };
			for (uint dy = 0; dy < 2; dy++)
				for (uint dx = 0; dx < 2; dx++)
				{
					const u8 *p = a + ((size_t)(y * 2 + dy) * w + (x * 2 + dx)) * 4;
					for (int k = 0; k < 4; k++)
						acc[k] += p[k];
				}
			const u8 *q = c + ((size_t)y * hw + x) * 4;
			for (int k = 0; k < 4; k++)
				tot += acc[k] / 4 > q[k] ? acc[k] / 4 - q[k] : q[k] - acc[k] / 4;
			n += 4;
		}
	FREE (a);
	FREE (c);
	return n ? (double)tot / n : -1;
}

// seam_score(): tile-boundary vs tile-interior pixel delta ratio, folded
// with the absolute interior delta so uniform noise doesn't masquerade as a
// perfect seam match (mirrors classify_tex.py exactly).
static double seam_score (const u8 *px, uint w, uint h, uint bw, uint bh)
{
	u64 edge_sum = 0, inner_sum = 0;
	uint edge_n = 0, inner_n = 0;
#define DELTA(x0, y0, x1, y1)                                                                      \
	({                                                                                             \
		const u8 *A = px + ((size_t)(y0) * w + (x0)) * 4, *B = px + ((size_t)(y1) * w + (x1)) * 4; \
		(uint) (abs (A[0] - B[0]) + abs (A[1] - B[1]) + abs (A[2] - B[2]) + abs (A[3] - B[3]));    \
	})

	for (uint y = 0; y < h; y++)
	{
		for (uint x = bw - 1; x + 1 < w; x += bw)
		{
			edge_sum += DELTA (x, y, x + 1, y);
			edge_n++;
		}
		for (uint x = 0; x + 1 < w; x += bw)
			if ((x + 1) % bw)
			{
				inner_sum += DELTA (x, y, x + 1, y);
				inner_n++;
			}
	}
	for (uint x = 0; x < w; x++)
	{
		for (uint y = bh - 1; y + 1 < h; y += bh)
		{
			edge_sum += DELTA (x, y, x, y + 1);
			edge_n++;
		}
		for (uint y = 0; y + 1 < h; y += bh)
			if ((y + 1) % bh)
			{
				inner_sum += DELTA (x, y, x, y + 1);
				inner_n++;
			}
	}
#undef DELTA
	if (!edge_n || !inner_n)
		return -1;
	const double e = (double)edge_sum / edge_n, i = (double)inner_sum / inner_n;
	return ((e + 1.0) / (i + 1.0)) * (1.0 + i / 24.0);
}

static double smoothness (const u8 *px, uint w, uint h)
{
	u64 tot = 0;
	uint n = 0;
	const uint stepy = h / 64 > 1 ? h / 64 : 1;
	const uint stepx = w / 64 > 1 ? w / 64 : 1;
	for (uint y = 0; y + 1 < h; y += stepy)
		for (uint x = 0; x + 1 < w; x += stepx)
		{
			const u8 *a = px + ((size_t)y * w + x) * 4;
			const u8 *r = px + ((size_t)y * w + x + 1) * 4;
			const u8 *d = px + ((size_t)(y + 1) * w + x) * 4;
			for (int k = 0; k < 4; k++)
			{
				tot += abs (a[k] - r[k]);
				tot += abs (a[k] - d[k]);
			}
			n += 8;
		}
	return n ? (double)tot / n : 1e18;
}

typedef struct tex_cand_t
{
	u8 fmt;
	uint w, h;
	uint dl;
} tex_cand_t;

// Locate the 24-byte footer at one of the four known offsets; returns true
// and fills *hoff/*w/*h on success. Mirrors classify_tex.py's find_footer().
static bool find_tex_footer (const u8 *data, uint size, uint *hoff, uint *w, uint *h)
{
	static const int offs[] = { -64, -32, -128, -96 };
	for (uint i = 0; i < 4; i++)
	{
		if ((int)size + offs[i] < 0)
			continue;
		const uint ho = size + offs[i];
		const u16 fw = xrd_le16 (data + ho), fh = xrd_le16 (data + ho + 2);
		bool wok = false, hok = false;
		for (uint d = 0; d < N_GX_DIMS; d++)
		{
			if (gx_dims[d] == fw)
				wok = true;
			if (gx_dims[d] == fh)
				hok = true;
		}
		if (wok && hok)
		{
			// log2(width)-2, i.e. bit_length(width)-3
			uint bl = 0;
			for (u16 t = fw; t; t >>= 1)
				bl++;
			if (data[ho + 4] == bl - 3)
			{
				*hoff = ho;
				*w = fw;
				*h = fh;
				return true;
			}
		}
	}
	return false;
}

// Shared classifier for both .tex (footer + mip-consistency) and .art/.img
// (no footer, seam-score only). 'has_footer' selects which of the three
// passes from classify_tex.py's classify() are attempted.
static enumError classify_excite_tex (
	excite_tex_t *tex, const u8 *data, uint size, bool want_footer)
{
	memset (tex, 0, sizeof (*tex));

	uint hoff = 0, fw = 0, fh = 0;
	const bool have_footer = want_footer && find_tex_footer (data, size, &hoff, &fw, &fh);

	// Mirrors classify_tex.py's classify(): the footer is only a hint. When
	// it's absent, or its claimed height doesn't match the actual stored mip
	// chain (a real observed case), fall back to size-128 and let the
	// exact-mip-chain-size search below recover the true dimensions.
	const uint dl = have_footer ? hoff : size >= 128 ? size - 128 : 0;
	if (!dl)
		return ERR_NOTHING_TO_DO;

	tex_cand_t cands[256];
	uint n_cand = 0;
#define ADD_CAND(f_, w_, h_, d_)                                                                   \
	do                                                                                             \
	{                                                                                              \
		bool dup = false;                                                                          \
		for (uint _i = 0; _i < n_cand; _i++)                                                       \
			if (cands[_i].fmt == (f_) && cands[_i].w == (w_) && cands[_i].h == (h_)                \
				&& cands[_i].dl == (d_))                                                           \
			{                                                                                      \
				dup = true;                                                                        \
				break;                                                                             \
			}                                                                                      \
		if (!dup && n_cand < 256)                                                                  \
			cands[n_cand++] = (tex_cand_t) { f_, w_, h_, d_ };                                     \
	} while (0)

	if (have_footer)
	{
		for (uint c = 0; c < N_GX_CANDIDATES; c++)
		{
			const u8 fmt = gx_candidates[c];
			if (gx_level_size (fmt, fw, fh) <= hoff)
				ADD_CAND (fmt, fw, fh, hoff);
		}
	}
	// exact mip-chain-size search (covers footers whose claimed height
	// doesn't match the actual stored chain, per classify_tex.py's comment)
	for (uint c = 0; c < N_GX_CANDIDATES; c++)
	{
		const u8 fmt = gx_candidates[c];
		for (uint wi = 0; wi < N_GX_DIMS; wi++)
			for (uint hi = 0; hi < N_GX_DIMS; hi++)
			{
				for (uint lv = 1; lv <= 10; lv++)
					if (gx_chain_size (fmt, gx_dims[wi], gx_dims[hi], lv) == dl)
					{
						ADD_CAND (fmt, gx_dims[wi], gx_dims[hi], dl);
						break;
					}
			}
	}
#undef ADD_CAND

	// pass 1: mip-chain consistency
	double best_score = 1e18;
	tex_cand_t best = { 0, 0, 0, 0 };
	bool have_best = false;
	for (uint i = 0; i < n_cand; i++)
	{
		const double m = mip_score (data, dl, cands[i].fmt, cands[i].w, cands[i].h);
		if (m < 0)
			continue;
		if (!have_best || m < best_score)
		{
			best_score = m;
			best = cands[i];
			have_best = true;
		}
	}
	if (have_best && best_score < 12.0)
	{
		u8 *rgba = gx_decode (best.fmt, best.w, best.h, data, best.dl);
		if (!rgba)
			return ERR_CANT_CREATE;
		tex->rgba = rgba;
		tex->width = best.w;
		tex->height = best.h;
		tex->gx_format = best.fmt;
		tex->score = (float)best_score;
		return ERR_OK;
	}

	// pass 2: tile-seam continuity (single-level images / no usable mips)
	have_best = false;
	best_score = 1e18;
	for (uint i = 0; i < n_cand; i++)
	{
		u8 *px = gx_decode (cands[i].fmt, cands[i].w, cands[i].h, data, cands[i].dl);
		if (!px)
			continue;
		if (count_distinct_sample (px, cands[i].w * cands[i].h) < 3)
		{
			FREE (px);
			continue;
		}
		const gxfmt_t *gf = find_gx_format (cands[i].fmt);
		double s = seam_score (px, cands[i].w, cands[i].h, gf->bw, gf->bh);
		if (s < 0)
			s = smoothness (px, cands[i].w, cands[i].h);
		const double ar = (double)(cands[i].w > cands[i].h ? cands[i].w : cands[i].h)
			/ (cands[i].w < cands[i].h ? cands[i].w : cands[i].h);
		if (ar >= 8)
			s *= 1.0 + 0.35 * (ar / 8.0);
		FREE (px);
		if (!have_best || s < best_score)
		{
			best_score = s;
			best = cands[i];
			have_best = true;
		}
	}
	if (have_best)
	{
		u8 *rgba = gx_decode (best.fmt, best.w, best.h, data, best.dl);
		if (!rgba)
			return ERR_CANT_CREATE;
		tex->rgba = rgba;
		tex->width = best.w;
		tex->height = best.h;
		tex->gx_format = best.fmt;
		tex->score = (float)best_score;
		return ERR_OK;
	}

	// pass 3: last resort, plain smoothness
	have_best = false;
	best_score = 1e18;
	for (uint i = 0; i < n_cand; i++)
	{
		u8 *px = gx_decode (cands[i].fmt, cands[i].w, cands[i].h, data, cands[i].dl);
		if (!px)
			continue;
		const double s = smoothness (px, cands[i].w, cands[i].h);
		FREE (px);
		if (!have_best || s < best_score)
		{
			best_score = s;
			best = cands[i];
			have_best = true;
		}
	}
	if (!have_best)
		return ERR_NOTHING_TO_DO;

	u8 *rgba = gx_decode (best.fmt, best.w, best.h, data, best.dl);
	if (!rgba)
		return ERR_CANT_CREATE;
	tex->rgba = rgba;
	tex->width = best.w;
	tex->height = best.h;
	tex->gx_format = best.fmt;
	tex->score = (float)best_score;
	return ERR_OK;
}

// Decode ExciteBots' 128-byte-header texture variant. Codes 0x40 and 0x41
// are respectively I4 and IA4 mip chains followed by a fixed 1024-byte
// renderer-data tail (which may be
// entirely zero); the other renderer codes use one of the ordinary GX
// formats without that tail. Keeping this
// here (rather than in ScanART) matters because the game uses the identical
// container for .tex, .art and .img resources.
static enumError scan_excite_header (excite_tex_t *tex, const u8 *data, uint size)
{
	if (size <= 128)
		return ERR_NOTHING_TO_DO;
	const uint w = xrd_le16 (data), h = xrd_le16 (data + 2);
	const uint levels = data[5] == 0x44 && data[4] == 2 ? 1 : data[4];
	if (!is_gx_dim (w) || !is_gx_dim (h) || levels < 1 || levels > 10 || data[5] < 0x40
		|| data[5] > 0x4f)
		return ERR_NOTHING_TO_DO;

	const u8 *pixels = data + 128;
	const uint psize = size - 128;
	if (data[5] == 0x40 || data[5] == 0x41)
	{
		const u8 fmt = data[5] == 0x40 ? GX_I4 : GX_IA4;
		const u64 chain = gx_chain_size (fmt, w, h, levels);
		if (chain + 1024 != psize)
			return ERR_NOTHING_TO_DO;
		tex->rgba = gx_decode (fmt, w, h, pixels, gx_level_size (fmt, w, h));
		if (!tex->rgba)
			return ERR_CANT_CREATE;
		tex->width = w;
		tex->height = h;
		tex->gx_format = fmt;
		tex->score = 0;
		return ERR_OK;
	}
	if (data[5] == 0x44)
	{
		const u8 fmt = GX_CMPR;
		const u64 single_chain = gx_chain_size (fmt, w, h, levels);
		if (single_chain * 2 == psize || single_chain == psize)
		{
			tex->rgba = gx_decode (fmt, w, h, pixels, gx_level_size (fmt, w, h));
			if (!tex->rgba)
				return ERR_CANT_CREATE;
			tex->width = w;
			tex->height = h;
			tex->gx_format = fmt;
			tex->score = 0;
			return ERR_OK;
		}
	}

	double best_score = 1e18;
	u8 best_fmt = 0;
	bool have = false;
	for (uint c = 0; c < N_GX_CANDIDATES; c++)
	{
		const u8 fmt = gx_candidates[c];
		if (gx_chain_size (fmt, w, h, levels) != psize)
			continue;
		double score = levels > 1 ? mip_score (pixels, psize, fmt, w, h) : -1;
		u8 *rgba = 0;
		if (score < 0)
		{
			rgba = gx_decode (fmt, w, h, pixels, gx_level_size (fmt, w, h));
			if (!rgba)
				continue;
			const gxfmt_t *gf = find_gx_format (fmt);
			score = seam_score (rgba, w, h, gf->bw, gf->bh);
			if (score < 0)
				score = smoothness (rgba, w, h);
			FREE (rgba);
		}
		if (!have || score < best_score)
		{
			have = true;
			best_score = score;
			best_fmt = fmt;
		}
	}
	if (!have)
		return ERR_NOTHING_TO_DO;
	tex->rgba = gx_decode (best_fmt, w, h, pixels, gx_level_size (best_fmt, w, h));
	if (!tex->rgba)
		return ERR_CANT_CREATE;
	tex->width = w;
	tex->height = h;
	tex->gx_format = best_fmt;
	tex->score = (float)best_score;
	return ERR_OK;
}

enumError ScanTEX (excite_tex_t *tex, const u8 *data, uint size)
{
	if (!tex || !data || size < 64)
		return ERR_NOTHING_TO_DO;
	const enumError herr = scan_excite_header (tex, data, size);
	if (herr == ERR_OK)
		return herr;
	return classify_excite_tex (tex, data, size, true);
}

// Some real .art GUI images are actually a colour+stencil pair: the decoded
// buffer comes out as one image twice its real height, colour on top and a
// stencil mask directly below (mask pixels: near-black RGB, alpha encoding
// the shape with 0=inside/255=outside -- inverted from a normal alpha
// channel). Ported from this repo's companion ExciteExtract research tool
// (art_export.py's looks_like_mask()): sample the lower half and require it
// to be mostly near-black (a real second picture wouldn't be) AND to have
// meaningfully bimodal alpha (a real picture's alpha, if any, wouldn't
// cluster at both extremes with little in between).
static bool excite_art_looks_like_mask (const u8 *rgba, uint w, uint h)
{
	const uint half_px = w * (h / 2);
	const uint total_px = w * h;
	const uint step = (total_px - half_px) / 4096 > 1 ? (total_px - half_px) / 4096 : 1;
	uint sampled = 0, dark = 0, a_lo = 0, a_hi = 0;
	for (uint i = half_px; i < total_px; i += step)
	{
		const u8 *p = rgba + (size_t)i * 4;
		if (((uint)p[0] + p[1] + p[2]) / 3 < 24)
			dark++;
		if (p[3] < 128)
			a_lo++;
		else
			a_hi++;
		sampled++;
	}
	if (!sampled)
		return false;
	const double dark_frac = (double)dark / sampled;
	const uint a_min = a_lo < a_hi ? a_lo : a_hi;
	const double bimodal_frac = (double)a_min / sampled;
	return dark_frac > 0.90 && bimodal_frac > 0.02;
}

// Recombine a colour-over-stencil stacked decode (tex->height == 2*width)
// into one real half-height RGBA image, in place: colour from the top half,
// alpha from the (inverted) stencil in the bottom half.
static void excite_art_recombine (excite_tex_t *tex)
{
	const uint w = tex->width, hh = tex->height / 2;
	u8 *out = MALLOC ((size_t)w * hh * 4);
	for (uint y = 0; y < hh; y++)
		for (uint x = 0; x < w; x++)
		{
			const u8 *c = tex->rgba + ((size_t)y * w + x) * 4;
			const u8 *m = tex->rgba + ((size_t)(y + hh) * w + x) * 4;
			u8 *o = out + ((size_t)y * w + x) * 4;
			o[0] = c[0];
			o[1] = c[1];
			o[2] = c[2];
			o[3] = 255 - m[3];
		}
	FREE (tex->rgba);
	tex->rgba = out;
	tex->height = hh;
}

enumError ScanART (excite_tex_t *tex, const u8 *data, uint size)
{
	if (!tex || !data || size <= 128)
		return ERR_NOTHING_TO_DO;
	const enumError herr = scan_excite_header (tex, data, size);
	if (herr == ERR_OK)
		return herr;
	// Every real sample is exactly 2^k + 128 bytes with a zeroed footer.
	const uint dl = size - 128;
	bool pow2 = dl && !(dl & (dl - 1));
	if (!pow2)
		return ERR_NOTHING_TO_DO;
	for (uint i = size - 128; i < size; i++)
		if (data[i])
			return ERR_NOTHING_TO_DO; // footer must be all-zero
	const enumError err = classify_excite_tex (tex, data, size, false);
	if (err == ERR_OK && tex->height == 2 * tex->width
		&& excite_art_looks_like_mask (tex->rgba, tex->width, tex->height))
		excite_art_recombine (tex);
	return err;
}

void ResetExciteTEX (excite_tex_t *tex)
{
	if (!tex)
		return;
	FREE (tex->rgba);
	memset (tex, 0, sizeof (*tex));
}

//-----------------------------------------------------------------------------
///////////////		.msh collision meshes				///////////////
//-----------------------------------------------------------------------------
//
// Monster Games PMsh collision resource, little-endian. The loader in
// ExciteBots' main.dol (FUN_8021bd50 in the analysed USA binary) fixes the
// six-word disk header into three pointer/count pairs at runtime:
//
//   +0x00 bucket-array placeholder   +0x04 bucket count
//   +0x08 position-array placeholder +0x0c position count
//   +0x10 triangle-array placeholder +0x14 triangle count
//   +0x18 bucket records (24 bytes each)
//         4 floats (centre XYZ + radius), first-triangle u32, count u32
//         positions (12 bytes each): float32 XYZ
//         triangles (60 bytes each): 4 u16 then 13 float32 values
//
// The first three header words contain build/runtime values and are replaced
// by the game, so only the counts describe the on-disk section locations.
// Each triangle's first u16 is collision metadata; the following three u16
// values index the position array. The remaining values contain the face
// normal, plane constant and edge planes. This layout was confirmed both from the
// game loader/raycast code and against all seven retail PMsh resources.

static inline float xrd_f32le (const u8 *p)
{
	u32 v = (u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0];
	float f;
	memcpy (&f, &v, 4);
	return f;
}
static inline u16 xrd_u16le (const u8 *p)
{
	return (u16)p[0] | (u16)p[1] << 8;
}

static inline u32 xrd_u32le (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

enumError DecodeExciteMSH (const u8 *data, uint size, ccp out_dae_path)
{
	if (!data || size < 24 || !out_dae_path)
		return ERR_NOTHING_TO_DO;
	const u32 n_buckets = xrd_u32le (data + 4);
	const u32 n_positions = xrd_u32le (data + 12);
	const u32 n_tris = xrd_u32le (data + 20);
	if (!n_positions || !n_tris)
		return ERR_NOTHING_TO_DO;

	const u64 positions_off = 24ull + (u64)n_buckets * 24;
	const u64 triangles_off = positions_off + (u64)n_positions * 12;
	const u64 expected_size = triangles_off + (u64)n_tris * 60;
	if (expected_size != size)
		return ERR_NOTHING_TO_DO;

	const u8 *position_data = data + (size_t)positions_off;
	const u8 *triangle_data = data + (size_t)triangles_off;
	for (u32 i = 0; i < n_tris; i++)
	{
		const u8 *tri = triangle_data + (size_t)i * 60;
		if (xrd_u16le (tri + 2) >= n_positions || xrd_u16le (tri + 4) >= n_positions
			|| xrd_u16le (tri + 6) >= n_positions)
			return ERR_NOTHING_TO_DO;
	}

	model_t model;
	memset (&model, 0, sizeof (model));
	mesh_t mesh;
	memset (&mesh, 0, sizeof (mesh));
	snprintf (mesh.name, sizeof (mesh.name), "collision");
	mesh.material_idx = -1;
	mesh.positions = MALLOC ((size_t)n_positions * sizeof (vec3_t));
	mesh.num_positions = n_positions;
	for (u32 i = 0; i < n_positions; i++)
	{
		const u8 *pos = position_data + (size_t)i * 12;
		mesh.positions[i].x = xrd_f32le (pos);
		mesh.positions[i].y = xrd_f32le (pos + 4);
		mesh.positions[i].z = xrd_f32le (pos + 8);
	}

	mesh.normals = MALLOC ((size_t)n_tris * sizeof (vec3_t));
	mesh.num_normals = n_tris;
	mesh.vertices = MALLOC ((size_t)n_tris * 3 * sizeof (vertex_t));
	mesh.num_vertices = (size_t)n_tris * 3;
	for (u32 i = 0; i < n_tris; i++)
	{
		const u8 *tri = triangle_data + (size_t)i * 60;
		mesh.normals[i].x = xrd_f32le (tri + 8);
		mesh.normals[i].y = xrd_f32le (tri + 12);
		mesh.normals[i].z = xrd_f32le (tri + 16);
		for (uint k = 0; k < 3; k++)
		{
			vertex_t *v = mesh.vertices + (size_t)i * 3 + k;
			v->position_idx = xrd_u16le (tri + (k + 1) * 2);
			v->normal_idx = i;
			v->texcoord_idx = v->matrix_idx = -1;
			v->color_idx[0] = v->color_idx[1] = -1;
			for (int j = 0; j < 7; j++)
				v->extra_texcoord_idx[j] = -1;
		}
	}
	model.meshes = &mesh;
	model.num_meshes = 1;

	const int rc = ExportModelToGLB (&model, out_dae_path);
	FREE (mesh.positions);
	FREE (mesh.normals);
	FREE (mesh.vertices);
	return rc == 0 ? ERR_OK : ERR_CANT_CREATE;
}

//-----------------------------------------------------------------------------
//
// PMsh encoder: inverse of DecodeExciteMSH above. All derived triangle values
// are recomputed with the formulas recovered from the retail corpus: the face
// plane is normalize(cross(p1-p0,p2-p0)) plus dot(n,p0), and each of the three
// edge slots stores the inward unit normal -normalize(cross(n,b-a)) over the
// edge cycle (p0,p1),(p1,p2),(p2,p0) -- verified against every stored float of
// the retail fixtures to float32 precision. Retail bucket spheres vary between
// exporter runs (goalback carries exact bbox-midpoint/max-distance balls while
// other resources hold slightly tighter hand-tuned ones); only enclosure
// matters for the game's broad-phase raycast, so this encoder emits plain
// <=16-triangle chunks with exact bbox-midpoint/max-distance spheres. The
// three pointer placeholders in the disk header are written as zero -- the
// game loader overwrites them at runtime anyway.

typedef struct
{
	u32 idx[3];
} msh_tri_t;

static inline void msh_u16le (u8 *p, u16 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}
static inline void msh_u32le (u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}
static inline void msh_f32le (u8 *p, double d)
{
	const float f = (float)d;
	u32 v;
	memcpy (&v, &f, 4);
	msh_u32le (p, v);
}

enumError EncodeExciteMSH (const model_t *model, ccp out_path)
{
	if (!model || !out_path)
		return ERR_INVALID_DATA;

	//--- collect triangles and deduplicate positions across all meshes -----

	uint pos_cap = 1024, num_pos = 0;
	vec3_t *positions = MALLOC (pos_cap * sizeof (*positions));
	uint tri_cap = 1024, num_tri = 0;
	msh_tri_t *tris = MALLOC (tri_cap * sizeof (*tris));

	// open-addressing hash on the raw 12-byte bit pattern
	uint hash_size = 1024;
	while (hash_size < 4 * 1024)
		hash_size <<= 1;
	int *hash_slot = MALLOC (hash_size * sizeof (*hash_slot));
	memset (hash_slot, -1, hash_size * sizeof (*hash_slot));

	enumError err = ERR_OK;
	for (uint mi = 0; mi < model->num_meshes && !err; mi++)
	{
		const mesh_t *m = model->meshes + mi;
		if (!m->num_vertices)
			continue;
		if (!m->positions || !m->vertices || m->num_vertices % 3)
		{
			err = ERROR0 (ERR_INVALID_DATA,
				"EncodeExciteMSH: mesh #%u has %zu vertices"
				" (multiple of 3 required)\n",
				mi, m->num_vertices);
			break;
		}
		for (size_t vi = 0; vi < m->num_vertices && !err; vi++)
		{
			const vertex_t *v = m->vertices + vi;
			if (v->position_idx < 0 || (size_t)v->position_idx >= m->num_positions)
			{
				err = ERROR0 (ERR_INVALID_DATA,
					"EncodeExciteMSH: mesh #%u vertex #%zu references"
					" position %d / %zu\n",
					mi, vi, v->position_idx, m->num_positions);
				break;
			}
			const vec3_t *src = m->positions + v->position_idx;

			if (vi % 3 == 0 && num_tri == tri_cap)
			{
				tri_cap *= 2;
				tris = REALLOC (tris, tri_cap * sizeof (*tris));
			}
			if (num_pos == pos_cap)
			{
				pos_cap *= 2;
				positions = REALLOC (positions, pos_cap * sizeof (*positions));
				hash_size <<= 1; // keep load factor bounded
				int *nh = MALLOC (hash_size * sizeof (*nh));
				memset (nh, -1, hash_size * sizeof (*nh));
				for (uint i = 0; i < num_pos; i++)
				{
					u32 key[3];
					memcpy (key, positions + i, 12);
					uint h = (key[0] * 0x9E3779B1u ^ key[1] * 0x85EBCA77u ^ key[2] * 0xC2B2AE3Du)
						& (hash_size - 1);
					while (nh[h] >= 0)
						h = (h + 1) & (hash_size - 1);
					nh[h] = i;
				}
				FREE (hash_slot);
				hash_slot = nh;
			}

			u32 key[3];
			memcpy (key, src, 12);
			uint h = (key[0] * 0x9E3779B1u ^ key[1] * 0x85EBCA77u ^ key[2] * 0xC2B2AE3Du)
				& (hash_size - 1);
			while (hash_slot[h] >= 0)
			{
				if (!memcmp (positions + hash_slot[h], src, sizeof (vec3_t)))
					break;
				h = (h + 1) & (hash_size - 1);
			}
			if (hash_slot[h] < 0)
			{
				positions[num_pos] = *src;
				hash_slot[h] = num_pos++;
			}
			tris[num_tri].idx[vi % 3] = hash_slot[h];
			if (vi % 3 == 2)
				num_tri++;
		}
	}
	FREE (hash_slot);

	if (err)
	{
		FREE (positions);
		FREE (tris);
		return err;
	}
	if (!num_tri)
	{
		FREE (positions);
		FREE (tris);
		return ERROR0 (ERR_INVALID_DATA, "EncodeExciteMSH: no triangles\n");
	}

	//--- build bucket chunks + assemble the file ---------------------------

	const uint n_buckets = (num_tri + 15) / 16;
	const size_t buckets_off = 24;
	const size_t positions_off = buckets_off + (size_t)n_buckets * 24;
	const size_t triangles_off = positions_off + (size_t)num_pos * 12;
	const size_t total = triangles_off + (size_t)num_tri * 60;

	u8 *buf = MALLOC (total);
	memset (buf, 0, total);
	msh_u32le (buf + 4, n_buckets);
	msh_u32le (buf + 12, num_pos);
	msh_u32le (buf + 20, num_tri);

	for (uint i = 0; i < num_pos; i++)
	{
		u8 *p = buf + positions_off + (size_t)i * 12;
		msh_f32le (p, positions[i].x);
		msh_f32le (p + 4, positions[i].y);
		msh_f32le (p + 8, positions[i].z);
	}

	for (uint b = 0; b < n_buckets; b++)
	{
		const uint first = b * 16;
		const uint end = first + 16 <= num_tri ? first + 16 : num_tri;

		// sphere over all member vertices: bbox midpoint centre, max distance
		double mn[3] = { 1e300, 1e300, 1e300 }, mx[3] = { -1e300, -1e300, -1e300 };
		for (uint t = first; t < end; t++)
			for (uint k = 0; k < 3; k++)
			{
				const vec3_t *v = positions + tris[t].idx[k];
				const double vx = v->x, vy = v->y, vz = v->z;
				const double vc[3] = { vx, vy, vz };
				for (uint c = 0; c < 3; c++)
				{
					if (vc[c] < mn[c])
						mn[c] = vc[c];
					if (vc[c] > mx[c])
						mx[c] = vc[c];
				}
			}
		double ctr[3];
		for (uint c = 0; c < 3; c++)
			ctr[c] = (mn[c] + mx[c]) / 2;
		double radius = 0.0;
		for (uint t = first; t < end; t++)
			for (uint k = 0; k < 3; k++)
			{
				const vec3_t *v = positions + tris[t].idx[k];
				const double dx = (double)v->x - ctr[0];
				const double dy = (double)v->y - ctr[1];
				const double dz = (double)v->z - ctr[2];
				const double d2 = dx * dx + dy * dy + dz * dz;
				if (d2 > radius)
					radius = d2;
			}
		radius = sqrt (radius);

		u8 *rec = buf + buckets_off + (size_t)b * 24;
		msh_f32le (rec, ctr[0]);
		msh_f32le (rec + 4, ctr[1]);
		msh_f32le (rec + 8, ctr[2]);
		msh_f32le (rec + 12, radius);
		msh_u32le (rec + 16, first);
		msh_u32le (rec + 20, end);
	}

	for (uint t = 0; t < num_tri && !err; t++)
	{
		u8 *rec = buf + triangles_off + (size_t)t * 60;
		msh_u16le (rec, 0); // collision metadata: semantics unknown, retail keeps small flags
		for (uint k = 0; k < 3; k++)
			msh_u16le (rec + 2 + 2 * k, tris[t].idx[k]);

		const vec3_t *v0 = positions + tris[t].idx[0];
		const vec3_t *v1 = positions + tris[t].idx[1];
		const vec3_t *v2 = positions + tris[t].idx[2];
		const double p0[3] = { v0->x, v0->y, v0->z };
		const double p1[3] = { v1->x, v1->y, v1->z };
		const double p2[3] = { v2->x, v2->y, v2->z };
		double e1[3], e2[3], n[3];
		for (uint c = 0; c < 3; c++)
		{
			e1[c] = p1[c] - p0[c];
			e2[c] = p2[c] - p0[c];
		}
		n[0] = e1[1] * e2[2] - e1[2] * e2[1];
		n[1] = e1[2] * e2[0] - e1[0] * e2[2];
		n[2] = e1[0] * e2[1] - e1[1] * e2[0];
		const double nl = sqrt (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		if (nl < 1e-12)
		{
			err = ERROR0 (ERR_INVALID_DATA,
				"EncodeExciteMSH: degenerate triangle %u"
				" (zero area)\n",
				t);
			break;
		}
		for (uint c = 0; c < 3; c++)
			n[c] /= nl;
		msh_f32le (rec + 8, n[0]);
		msh_f32le (rec + 12, n[1]);
		msh_f32le (rec + 16, n[2]);
		msh_f32le (rec + 20, n[0] * p0[0] + n[1] * p0[1] + n[2] * p0[2]);

		static const uint edge_a[3] = { 0, 1, 2 }, edge_b[3] = { 1, 2, 0 };
		for (uint e = 0; e < 3; e++)
		{
			const double *a = edge_a[e] == 0 ? p0 : edge_a[e] == 1 ? p1 : p2;
			const double *b = edge_b[e] == 0 ? p0 : edge_b[e] == 1 ? p1 : p2;
			double en[3];
			for (uint c = 0; c < 3; c++)
				en[c] = b[c] - a[c];
			// inward unit normal: -normalize(cross(n, b-a))
			double cx = n[1] * en[2] - n[2] * en[1];
			double cy = n[2] * en[0] - n[0] * en[2];
			double cz = n[0] * en[1] - n[1] * en[0];
			const double el = sqrt (cx * cx + cy * cy + cz * cz);
			if (el < 1e-12)
			{
				err = ERROR0 (ERR_INVALID_DATA,
					"EncodeExciteMSH: degenerate triangle %u"
					" (zero-length edge)\n",
					t);
				break;
			}
			msh_f32le (rec + 24 + 12 * e, -cx / el);
			msh_f32le (rec + 28 + 12 * e, -cy / el);
			msh_f32le (rec + 32 + 12 * e, -cz / el);
		}
	}

	FREE (positions);
	FREE (tris);
	if (err)
	{
		FREE (buf);
		return err;
	}

	const enumError rc = SaveFILE (out_path, 0, true, buf, (uint)total, 0);
	FREE (buf);
	if (rc <= ERR_WARNING && verbose >= 0)
		fprintf (stdlog, "ENCODE MSH: -> %s (%zu bytes, %u tris, %u buckets)\n", out_path, total,
			num_tri, n_buckets);
	return rc <= ERR_WARNING ? ERR_OK : rc;
}

//-----------------------------------------------------------------------------
///////////////		.mod 3D models (Monster Games NDL3/NDL2)	///////////////
//-----------------------------------------------------------------------------
//
// Monster Games "3LDN"/"2LDN" ("NDL3"/"NDL2" read as a little-endian magic)
// model container -- Excite Truck's older "2LDN" revision and ExciteBots'
// "3LDN" revision share this same shape. Ported from this repo's companion
// ExciteExtract research tool (mod_parse.py/mod_export.py), which validated
// against the full retail corpus of both games: 135/135 Excite Truck and
// 193/203 ExciteBots real .mod files decode (the ExciteBots gap is exactly
// the intentionally-separate .msh collision-mesh files, not .mod failures).
// A prior pass through this same file only recognised 3/196 samples because
// it required the magic at file offset 0 and hardcoded a single 0x82-flags,
// one-tristrip-call shape; both restrictions were wrong, not narrow-but-
// correct -- most real files carry a small texture-filename table *before*
// the magic, and the geometry format is fully self-describing via the GX
// vertex-attribute-table (VAT) register writes embedded in the display list
// itself, so there is no need to hardcode flag values or index widths at all.
//
// Header (14 x u32le, magic at file offset 'm' -- may be > 0):
//   +0x00  "3LDN"/"2LDN" magic
//   +0x04  dl_end -- end offset (from 'm') of the display-list region. NDL2
//          stores this as a u16 with the high half filled with the 0xe3e3
//          filler byte pair, not a real u32; detect and mask that off.
//   +0x08  version constant (0x0e1e in both games; not relied upon)
//   +0x0c  format/flags constant (0x82 in most samples, but other real
//          values exist too -- irrelevant now that the VAT is read directly)
//   +0x18  f32 bounding-radius (not geometry data; not used)
//   +0x1c  vertex-position count
//   +0x20..0x30  more fields, still unmapped; not used
//
// The display list is genuine GX: 0x08 writes a CP register (used here only
// for VAT group A at address 0x70, which carries the position/texcoord
// element count, numeric format, and fixed-point shift -- see the GX
// hardware register reference), 0x10/0x61 write XF/BP registers (skipped),
// and 0x80..0xB8 are primitive draw calls (low 3 bits select vertex-format
// slot 0; the primitive kind is in bits 3-6: 0x80 quads, 0x88 quads2, 0x90
// triangles, 0x98 tristrip, 0xA0 fan, 0xA8 lines, 0xB0 linestrip, 0xB8
// points), each followed by a u16be vertex count and that many index tuples
// of a per-file, not-separately-stored width ("bytes per vertex", bpv):
// the first byte of each tuple is always the position index, the last byte
// (when bpv>1) is the texcoord index. bpv is recovered by brute force --
// try 1..8, keep whichever consumes the display list into a sequence of
// well-formed opcodes ending in the 0xe3 filler or end-of-buffer with no
// out-of-bounds reads; this self-validates because a wrong bpv drifts the
// byte stream into garbage opcodes almost immediately.
//
// The display list's start is found by its CP-register preamble signature
// (0x08 0x70 <4 bytes> 0x08 0x80 <4 bytes> 0x08 0x90) rather than trusting
// a version-specific size/offset field, since NDL2's 'dl_end' field shape
// differs from NDL3's.
//
// Geometry data begins at file offset 'm'+0x40: the position array (vertex-
// position-count entries, component count/format/fixed-point shift from
// the VAT), followed immediately by the texcoord array (component count/
// format/shift also from the VAT; its length isn't stored anywhere, so it
// is sized from the highest texcoord index actually used by the display
// list). Every real sample seen encodes only one UV channel -- multi-UV/
// vertex-colour/normal channels are not modelled here and any file needing
// them for its VAT-declared attributes beyond position+texcoord0 is not
// specially detected, since none of the validated corpus used them.
//-----------------------------------------------------------------------------

static inline u32 xrd_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline u32 xrd_le32 (const u8 *p)
{
	return (u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0];
}

static inline float xrd_f32be (const u8 *p)
{
	const u32 v = xrd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static inline s16 xrd_be16s (const u8 *p)
{
	return (s16)xrd_be16 (p);
}

// One recognised GX draw call inside the display list: 'dl_off' is the byte
// offset (from the start of the display-list buffer) of its first index
// tuple, 'cnt' vertices wide, each tuple 'bpv' bytes (bpv lives on the
// caller, one value for the whole display list).
typedef struct mod_prim_t
{
	int kind; // MOD_QUADS..MOD_POINTS
	u16 cnt;
	uint dl_off;
} mod_prim_t;

enum
{
	MOD_QUADS,
	MOD_QUADS2,
	MOD_TRIANGLES,
	MOD_TRISTRIP,
	MOD_TRIFAN,
	MOD_LINES,
	MOD_LINESTRIP,
	MOD_POINTS
};

// Walk 'dl' assuming 'bpv' index bytes per vertex. On success, returns a
// MALLOC'd array of the primitives found (caller FREEs) and the total
// vertex count summed across them (used by the caller to pick the winning
// bpv). Returns false -- freeing nothing -- on any malformed/out-of-bounds
// opcode, which is how a wrong bpv guess is rejected.
static bool mod_try_parse_dl (const u8 *dl, uint n, uint bpv, mod_prim_t **out_prims,
	uint *out_nprims, uint *out_total, u32 vat[256], u8 vat_set[256])
{
	uint cap = 8, np = 0, total = 0, p = 0;
	mod_prim_t *prims = MALLOC (cap * sizeof (*prims));
	memset (vat_set, 0, 256);

	while (p < n)
	{
		const u8 op = dl[p];
		if (op == 0x00)
		{
			p++;
			continue;
		}
		if (op == 0xe3)
			break;

		if (op == 0x08)
		{
			if (p + 6 > n)
				goto fail;
			vat[dl[p + 1]] = xrd_be32 (dl + p + 2);
			vat_set[dl[p + 1]] = 1;
			p += 6;
			continue;
		}
		if (op == 0x10)
		{
			if (p + 5 > n)
				goto fail;
			const u64 adv = 5 + ((u64)xrd_be16 (dl + p + 1) + 1) * 4;
			if (p + adv > n)
				goto fail;
			p += adv;
			continue;
		}
		if (op == 0x61)
		{
			if (p + 5 > n)
				goto fail;
			p += 5;
			continue;
		}

		int kind;
		switch (op & 0xf8)
		{
			case 0x80:
				kind = MOD_QUADS;
				break;
			case 0x88:
				kind = MOD_QUADS2;
				break;
			case 0x90:
				kind = MOD_TRIANGLES;
				break;
			case 0x98:
				kind = MOD_TRISTRIP;
				break;
			case 0xA0:
				kind = MOD_TRIFAN;
				break;
			case 0xA8:
				kind = MOD_LINES;
				break;
			case 0xB0:
				kind = MOD_LINESTRIP;
				break;
			case 0xB8:
				kind = MOD_POINTS;
				break;
			default:
				goto fail;
		}

		if (p + 3 > n)
			goto fail;
		const u16 cnt = xrd_be16 (dl + p + 1);
		p += 3;
		const u64 need = (u64)cnt * bpv;
		if (p + need > n)
			goto fail;

		if (np == cap)
		{
			cap *= 2;
			prims = REALLOC (prims, cap * sizeof (*prims));
		}
		prims[np].kind = kind;
		prims[np].cnt = cnt;
		prims[np].dl_off = p;
		np++;
		total += cnt;
		p += need;
	}

	if (!np)
		goto fail;
	*out_prims = prims;
	*out_nprims = np;
	*out_total = total;
	return true;

fail:
	FREE (prims);
	return false;
}

// Read a fixed-point/float GX attribute array (big-endian, GX convention).
// fmt: 0=u8 1=s8 2=u16 3=s16 4=f32. Returns false (nothing allocated) if the
// array would run past 'size'.
static bool mod_read_attr (
	const u8 *data, uint size, uint off, uint count, uint fmt, uint ncomp, uint shift, float **out)
{
	static const uint fmt_sz[5] = { 1, 1, 2, 2, 4 };
	if (fmt > 4)
		return false;
	const uint sz = fmt_sz[fmt];
	if ((u64)off + (u64)count * ncomp * sz > size)
		return false;

	const float scale = fmt == 4 ? 1.0f : 1.0f / (float)(1u << shift);
	float *out_arr = MALLOC (count * ncomp * sizeof (float));
	const u8 *p = data + off;
	for (uint i = 0; i < count * ncomp; i++, p += sz)
	{
		s32 raw;
		switch (fmt)
		{
			case 0:
				raw = p[0];
				break;
			case 1:
				raw = (s8)p[0];
				break;
			case 2:
				raw = xrd_be16 (p);
				break;
			case 3:
				raw = (s16)xrd_be16 (p);
				break;
			default:
				out_arr[i] = xrd_f32be (p);
				continue;
		}
		out_arr[i] = raw * scale;
	}
	*out = out_arr;
	return true;
}

// Helper to decode a single NDL chunk into a mesh_t
static bool mod_decode_ndl_chunk (const u8 *data, uint size, uint m, int mat_idx, uint chunk_idx, mesh_t *out_mesh)
{
	if (m + 0x38 > size)
		return false;

	u32 h[14];
	for (int i = 0; i < 14; i++)
		h[i] = xrd_le32 (data + m + i * 4);
	u32 dl_end = h[1];
	if ((dl_end >> 16) == 0xe3e3)
		dl_end &= 0xffff; // NDL2 u16 quirk
	const u32 n_pos = h[7];
	if (!n_pos || n_pos > 100000)
		return false;

	const u32 pos_off = h[9] >= m && h[9] < size ? h[9] : m + 0x40;
	const u32 second_off = h[10] >= m && h[10] < size ? h[10] : 0;
	const u32 third_off = h[11] >= m && h[11] < size ? h[11] : 0;
	const u32 dl_hdr_off = h[13] >= m && h[13] < size ? h[13] : 0;
	const u32 dl_hdr_sz = h[12] && dl_hdr_off + h[12] <= size ? h[12] : 0;

	uint dl_start = dl_hdr_off ? dl_hdr_off : 0;
	uint dl_size = dl_hdr_sz;

	if (!dl_start)
	{
		const uint hi = dl_end && m + dl_end <= size ? m + dl_end : size;
		for (uint q = m; q + 14 <= hi; q++)
		{
			if (data[q] == 0x08 && data[q + 1] == 0x70 && data[q + 6] == 0x08 && data[q + 7] == 0x80
				&& data[q + 12] == 0x08 && data[q + 13] == 0x90)
			{
				dl_start = q;
				dl_size = dl_end > (q - m) && m + dl_end <= size ? dl_end - (q - m) : size - dl_start;
				break;
			}
		}
	}
	if (!dl_start)
		return false;

	const u8 *dl = data + dl_start;

	mod_prim_t *best_prims = 0;
	uint best_np = 0, best_total = 0, best_bpv = 0;
	u32 vat[256];
	u8 vat_set[256];
	u32 best_vat[256];
	u8 best_vat_set[256];
	for (uint bpv = 1; bpv <= 8; bpv++)
	{
		mod_prim_t *prims;
		uint np, total;
		if (!mod_try_parse_dl (dl, dl_size, bpv, &prims, &np, &total, vat, vat_set))
			continue;
		if (total > best_total)
		{
			FREE (best_prims);
			best_prims = prims;
			best_np = np;
			best_total = total;
			best_bpv = bpv;
			memcpy (best_vat, vat, sizeof (vat));
			memcpy (best_vat_set, vat_set, sizeof (vat_set));
		}
		else
			FREE (prims);
	}
	if (!best_prims)
		return false;
	if (!best_vat_set[0x70])
	{
		FREE (best_prims);
		return false;
	}

	const u32 va = best_vat[0x70];
	const uint pos_elem = (va >> 0) & 1, pos_fmt = (va >> 1) & 7, pos_shft = (va >> 4) & 0x1f;
	const uint tex_elem = (va >> 21) & 1, tex_fmt = (va >> 22) & 7, tex_shft = (va >> 25) & 0x1f;
	const uint pos_n = pos_elem ? 3 : 2;
	const uint tex_n = tex_elem ? 2 : 1;

	static const uint fmt_sz[5] = { 1, 1, 2, 2, 4 };
	if (pos_fmt > 4)
	{
		FREE (best_prims);
		return false;
	}
	const uint tex_off = third_off ? third_off : (second_off ? second_off : pos_off + n_pos * pos_n * fmt_sz[pos_fmt]);

	uint max_tex = 0;
	for (uint i = 0; i < best_np; i++)
	{
		const mod_prim_t *pr = best_prims + i;
		if (best_bpv < 2)
			continue;
		for (uint v = 0; v < pr->cnt; v++)
		{
			const u8 ti = dl[pr->dl_off + v * best_bpv + best_bpv - 1];
			if (ti > max_tex)
				max_tex = ti;
		}
	}
	const uint n_tex = max_tex + 1;

	float *pos_f = 0, *tex_f = 0;
	if (!mod_read_attr (data, size, pos_off, n_pos, pos_fmt, pos_n, pos_shft, &pos_f)
		|| (tex_fmt <= 4
			&& !mod_read_attr (data, size, tex_off, n_tex, tex_fmt, tex_n, tex_shft, &tex_f)))
	{
		FREE (best_prims);
		FREE (pos_f);
		FREE (tex_f);
		return false;
	}

	memset (out_mesh, 0, sizeof (*out_mesh));
	snprintf (out_mesh->name, sizeof (out_mesh->name), "mesh_%u", chunk_idx);
	out_mesh->material_idx = mat_idx;

	out_mesh->positions = MALLOC (n_pos * sizeof (vec3_t));
	out_mesh->num_positions = n_pos;
	for (uint i = 0; i < n_pos; i++)
	{
		out_mesh->positions[i].x = pos_f[i * pos_n];
		out_mesh->positions[i].y = pos_f[i * pos_n + 1];
		out_mesh->positions[i].z = pos_n > 2 ? pos_f[i * pos_n + 2] : 0.0f;
	}

	out_mesh->texcoords = MALLOC (n_tex * sizeof (vec2_t));
	out_mesh->num_texcoords = n_tex;
	for (uint i = 0; i < n_tex; i++)
	{
		out_mesh->texcoords[i].u = tex_f[i * tex_n];
		out_mesh->texcoords[i].v = tex_n > 1 ? tex_f[i * tex_n + 1] : 0.0f;
	}

	uint tri_cap = best_total * 2 + 8, tri_n = 0;
	vertex_t *tris = MALLOC (tri_cap * sizeof (vertex_t));

#define MOD_CORNER(idxarr)                                                                         \
	do                                                                                             \
	{                                                                                              \
		if (tri_n == tri_cap)                                                                      \
		{                                                                                          \
			tri_cap *= 2;                                                                          \
			tris = REALLOC (tris, tri_cap * sizeof (vertex_t));                                    \
		}                                                                                          \
		vertex_t *dv = tris + tri_n++;                                                             \
		const u8 pi = dl[pr->dl_off + (idxarr) * best_bpv];                                        \
		const u8 ti = best_bpv > 1 ? dl[pr->dl_off + (idxarr) * best_bpv + best_bpv - 1] : 0;      \
		dv->position_idx = pi < n_pos ? pi : 0;                                                    \
		dv->texcoord_idx = ti < n_tex ? ti : 0;                                                    \
		dv->normal_idx = dv->matrix_idx = -1;                                                      \
		dv->color_idx[0] = dv->color_idx[1] = -1;                                                  \
		for (int e = 0; e < 7; e++)                                                                \
			dv->extra_texcoord_idx[e] = -1;                                                        \
	} while (0)

	for (uint i = 0; i < best_np; i++)
	{
		const mod_prim_t *pr = best_prims + i;
		switch (pr->kind)
		{
			case MOD_TRISTRIP:
				for (uint t = 0; t + 2 < pr->cnt; t++)
				{
					if (t & 1)
					{
						MOD_CORNER (t + 1);
						MOD_CORNER (t);
						MOD_CORNER (t + 2);
					}
					else
					{
						MOD_CORNER (t);
						MOD_CORNER (t + 1);
						MOD_CORNER (t + 2);
					}
				}
				break;
			case MOD_TRIFAN:
				for (uint t = 1; t + 1 < pr->cnt; t++)
				{
					MOD_CORNER (0);
					MOD_CORNER (t);
					MOD_CORNER (t + 1);
				}
				break;
			case MOD_TRIANGLES:
				for (uint t = 0; t + 2 < pr->cnt; t += 3)
				{
					MOD_CORNER (t);
					MOD_CORNER (t + 1);
					MOD_CORNER (t + 2);
				}
				break;
			case MOD_QUADS:
			case MOD_QUADS2:
				for (uint t = 0; t + 3 < pr->cnt; t += 4)
				{
					MOD_CORNER (t);
					MOD_CORNER (t + 1);
					MOD_CORNER (t + 2);
					MOD_CORNER (t);
					MOD_CORNER (t + 2);
					MOD_CORNER (t + 3);
				}
				break;
			default:
				break;
		}
	}
#undef MOD_CORNER

	out_mesh->vertices = tris;
	out_mesh->num_vertices = tri_n;

	FREE (best_prims);
	FREE (pos_f);
	FREE (tex_f);
	return tri_n > 0;
}

// Extract a null/e3-terminated ASCII texture name starting at pos
static bool mod_extract_tex_name (const u8 *data, uint size, uint pos, char *out, size_t out_size)
{
	if (pos >= size)
		return false;
	uint end = pos;
	while (end < size && data[end] >= 32 && data[end] <= 126 && data[end] != 0 && data[end] != 0xe3)
		end++;
	uint len = end - pos;
	if (!len)
		return false;
	if (len > 4 && !strcasecmp ((const char*)data + pos + len - 4, ".tex"))
		len -= 4;
	if (len >= out_size)
		len = (uint)out_size - 1;
	memcpy (out, data + pos, len);
	out[len] = 0;
	return true;
}

// Parse material descriptor at desc_off, extracting texture references
static int mod_find_or_create_material (
	const u8 *data, uint size, uint desc_off, material_t *materials, uint *num_materials, uint max_materials)
{
	if (!desc_off || desc_off + 4 > size)
		return -1;

	// Check if this descriptor offset was already parsed into an existing material
	char mat_name[64];
	snprintf (mat_name, sizeof (mat_name), "mat_0x%x", desc_off);
	for (uint i = 0; i < *num_materials; i++)
	{
		if (!strcmp (materials[i].name, mat_name))
			return (int)i;
	}

	if (*num_materials >= max_materials)
		return -1;

	material_t *mat = &materials[*num_materials];
	memset (mat, 0, sizeof (*mat));
	snprintf (mat->name, sizeof (mat->name), "%s", mat_name);
	mat->diffuse[0] = mat->diffuse[1] = mat->diffuse[2] = mat->diffuse[3] = 1.0f;

	const u32 num_layers = xrd_le32 (data + desc_off);
	if (num_layers >= 1 && num_layers <= 8)
	{
		char tex_name[64];
		if (num_layers == 1)
		{
			if (mod_extract_tex_name (data, size, desc_off + 16, tex_name, sizeof (tex_name)))
			{
				snprintf (mat->textures[0], sizeof (mat->textures[0]), "%s", tex_name);
				mat->wrap_s[0] = mat->wrap_t[0] = 1;
				mat->min_filter[0] = mat->mag_filter[0] = 1;
				mat->num_textures = 1;
			}
		}
		else if (num_layers == 2)
		{
			if (mod_extract_tex_name (data, size, desc_off + 20, tex_name, sizeof (tex_name)))
			{
				snprintf (mat->textures[mat->num_textures], sizeof (mat->textures[0]), "%s", tex_name);
				mat->wrap_s[mat->num_textures] = mat->wrap_t[mat->num_textures] = 1;
				mat->min_filter[mat->num_textures] = mat->mag_filter[mat->num_textures] = 1;
				mat->num_textures++;
			}
			const u32 ptr2 = xrd_le32 (data + desc_off + 12);
			if (ptr2 < size && mod_extract_tex_name (data, size, ptr2 + 4, tex_name, sizeof (tex_name)))
			{
				snprintf (mat->textures[mat->num_textures], sizeof (mat->textures[0]), "%s", tex_name);
				mat->wrap_s[mat->num_textures] = mat->wrap_t[mat->num_textures] = 1;
				mat->min_filter[mat->num_textures] = mat->mag_filter[mat->num_textures] = 1;
				mat->num_textures++;
			}
		}
		else
		{
			if (mod_extract_tex_name (data, size, desc_off + 24, tex_name, sizeof (tex_name)))
			{
				snprintf (mat->textures[mat->num_textures], sizeof (mat->textures[0]), "%s", tex_name);
				mat->wrap_s[mat->num_textures] = mat->wrap_t[mat->num_textures] = 1;
				mat->min_filter[mat->num_textures] = mat->mag_filter[mat->num_textures] = 1;
				mat->num_textures++;
			}
			for (u32 L = 1; L < num_layers && mat->num_textures < 8; L++)
			{
				const u32 ptr = xrd_le32 (data + desc_off + 8 + L * 4);
				if (ptr < size && mod_extract_tex_name (data, size, ptr + 4, tex_name, sizeof (tex_name)))
				{
					snprintf (mat->textures[mat->num_textures], sizeof (mat->textures[0]), "%s", tex_name);
					mat->wrap_s[mat->num_textures] = mat->wrap_t[mat->num_textures] = 1;
					mat->min_filter[mat->num_textures] = mat->mag_filter[mat->num_textures] = 1;
					mat->num_textures++;
				}
			}
		}
	}

	const int idx = (int)(*num_materials);
	(*num_materials)++;
	return idx;
}

enumError DecodeExciteMOD (const u8 *data, uint size, ccp out_path)
{
	if (!data || size < 0x40)
		return ERR_NOTHING_TO_DO;

	const u32 num_entries = xrd_le32 (data);
	const u32 table_off = xrd_le32 (data + 4);

	bool is_multi = false;
	if (num_entries > 0 && num_entries < 2000 && table_off >= 0x40 && table_off + num_entries * 16 <= size)
		is_multi = true;

	material_t materials[64];
	uint num_materials = 0;
	memset (materials, 0, sizeof (materials));

	mesh_t *meshes = 0;
	uint num_meshes = 0;

	if (is_multi)
	{
		meshes = CALLOC (num_entries, sizeof (mesh_t));
		if (!meshes)
			return ERR_CANT_CREATE;

		for (uint i = 0; i < num_entries; i++)
		{
			const uint ent_off = table_off + i * 16;
			const u32 desc_off = xrd_le32 (data + ent_off + 8);
			const u32 ndl_off = xrd_le32 (data + ent_off + 12);

			const int mat_idx = mod_find_or_create_material (
				data, size, desc_off, materials, &num_materials, 64);

			mesh_t mesh;
			if (mod_decode_ndl_chunk (data, size, ndl_off, mat_idx, i, &mesh))
			{
				meshes[num_meshes++] = mesh;
			}
		}
	}
	else
	{
		// Single NDL fallback
		uint m = 0;
		bool found = false;
		for (; m + 4 <= size; m++)
		{
			if (!memcmp (data + m, "3LDN", 4) || !memcmp (data + m, "2LDN", 4))
			{
				found = true;
				break;
			}
		}
		if (!found || m + 0x38 > size)
			return ERR_NOTHING_TO_DO;

		meshes = CALLOC (1, sizeof (mesh_t));
		if (!meshes)
			return ERR_CANT_CREATE;

		mesh_t mesh;
		if (mod_decode_ndl_chunk (data, size, m, -1, 0, &mesh))
		{
			meshes[num_meshes++] = mesh;
		}
	}

	if (!num_meshes)
	{
		FREE (meshes);
		return ERR_NOTHING_TO_DO;
	}

	model_t model;
	memset (&model, 0, sizeof (model));
	model.meshes = meshes;
	model.num_meshes = num_meshes;
	model.materials = num_materials ? materials : 0;
	model.num_materials = num_materials;

	const enumError rc = (ExportModelToGLB (&model, out_path) == 0) ? ERR_OK : ERR_CANT_CREATE;

	for (uint i = 0; i < num_meshes; i++)
	{
		FREE (meshes[i].positions);
		FREE (meshes[i].texcoords);
		FREE (meshes[i].vertices);
	}
	FREE (meshes);

	return rc;
}

//-----------------------------------------------------------------------------
//
// NDL3 encoder: inverse of DecodeExciteMOD above, sharing its conventions:
// geometry-only output (the optional texture-filename table that some real
// files carry ahead of the magic has an unrecovered layout, so none is
// written), positions stored big-endian f32 via VAT group A, texcoords as
// s16 with the retail shift 13, and one GX TRIANGLES (0x90) draw call per
// <=21845-triangle chunk using 2-byte index tuples (position index, then
// texcoord index). GX tuple indices are single bytes, so models with more
// than 255 unique positions or texcoords are rejected rather than silently
// truncated -- matching the retail exporters, which batch their geometry
// under the same limit. Unknown header fields (tex_desc_off/off_c/off_d and
// the post-display-list footer block) are written zero; the display-list
// size field (+0x30) is filled correctly and the pointer-free header makes
// the file self-describing for the game loader.

static inline void mod_u16be (u8 *p, u16 v)
{
	p[0] = (u8)(v >> 8);
	p[1] = (u8)v;
}
static inline void mod_u32be (u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

typedef struct
{
	u32 pos_idx, tex_idx;
} mod_corner_t;

enumError EncodeExciteMOD (const model_t *model, ccp out_path)
{
	if (!model || !out_path)
		return ERR_INVALID_DATA;

	//--- flatten corners; deduplicate positions and texcoords --------------
	//
	// Both index spaces are capped at 255 entries because GX vertex tuples
	// carry single-byte indices. Dedup uses FNV-1a over the raw component
	// bytes with linear probing; a 1024-slot table is plenty for that cap.

	enum
	{
		MOD_HASH_SIZE = 1024
	};

	uint num_pos = 0;
	vec3_t *positions = MALLOC (256 * sizeof (*positions));
	int *pos_hash = MALLOC (MOD_HASH_SIZE * sizeof (int));
	memset (pos_hash, -1, sizeof (int) * MOD_HASH_SIZE);

	uint num_tex = 0;
	vec2_t *texcoords = MALLOC (256 * sizeof (*texcoords));
	int *tex_hash = MALLOC (MOD_HASH_SIZE * sizeof (int));
	memset (tex_hash, -1, sizeof (int) * MOD_HASH_SIZE);

	uint corn_cap = 1024, num_corn = 0;
	mod_corner_t *corners = MALLOC (corn_cap * sizeof (*corners));

	enumError err = ERR_OK;

// open-addressing dedup on raw component bytes; arrays are preallocated
// for 256 entries so no growth is possible under the 255-entry cap
#define MOD_DEDUP_LOOKUP(name, arrname, numname, srcptr, ncomp, outidx)                            \
	do                                                                                             \
	{                                                                                              \
		u32 key_[ncomp];                                                                           \
		memcpy (key_, srcptr, sizeof (key_));                                                      \
		uint h_ = 2166136261u;                                                                     \
		for (uint c_ = 0; c_ < ncomp; c_++)                                                        \
			h_ = (h_ ^ key_[c_]) * 16777619u;                                                      \
		h_ &= (MOD_HASH_SIZE - 1);                                                                 \
		while (name[h_] >= 0 && memcmp (arrname + name[h_], srcptr, sizeof (key_)))                \
			h_ = (h_ + 1) & (MOD_HASH_SIZE - 1);                                                   \
		if (name[h_] < 0)                                                                          \
		{                                                                                          \
			if (numname == 255)                                                                    \
			{                                                                                      \
				err = ERROR0 (ERR_INVALID_DATA,                                                    \
					"EncodeExciteMOD: more than 255 unique " #arrname                              \
					" (GX tuple indices are single bytes)\n");                                     \
				break;                                                                             \
			}                                                                                      \
			arrname[numname] = *(srcptr);                                                          \
			name[h_] = numname++;                                                                  \
		}                                                                                          \
		outidx = name[h_];                                                                         \
	} while (0)

	for (uint mi = 0; mi < model->num_meshes && !err; mi++)
	{
		const mesh_t *m = model->meshes + mi;
		if (!m->num_vertices)
			continue;
		if (!m->positions || !m->vertices || m->num_vertices % 3)
		{
			err = ERROR0 (ERR_INVALID_DATA,
				"EncodeExciteMOD: mesh #%u has %zu vertices"
				" (multiple of 3 required)\n",
				mi, m->num_vertices);
			break;
		}
		if (num_corn + m->num_vertices > corn_cap)
		{
			while (num_corn + m->num_vertices > corn_cap)
				corn_cap *= 2;
			corners = REALLOC (corners, corn_cap * sizeof (*corners));
		}
		for (size_t vi = 0; vi < m->num_vertices && !err; vi++)
		{
			const vertex_t *v = m->vertices + vi;
			if (v->position_idx < 0 || (size_t)v->position_idx >= m->num_positions)
			{
				err = ERROR0 (ERR_INVALID_DATA,
					"EncodeExciteMOD: mesh #%u vertex #%zu references"
					" position %d / %zu\n",
					mi, vi, v->position_idx, m->num_positions);
				break;
			}
			vec3_t p3 = m->positions[v->position_idx];
			u32 pi = 0, ti = 0;
			MOD_DEDUP_LOOKUP (pos_hash, positions, num_pos, (&p3), 3, pi);
			if (err)
				break;
			vec2_t t2 = { 0, 0 };
			if (m->texcoords && v->texcoord_idx >= 0 && (size_t)v->texcoord_idx < m->num_texcoords)
				t2 = m->texcoords[v->texcoord_idx];
			MOD_DEDUP_LOOKUP (tex_hash, texcoords, num_tex, (&t2), 2, ti);
			if (err)
				break;
			corners[num_corn].pos_idx = pi;
			corners[num_corn].tex_idx = ti;
			num_corn++;
		}
	}
	FREE (pos_hash);
	FREE (tex_hash);
#undef MOD_DEDUP_LOOKUP

	if (err || !num_corn)
	{
		if (!err)
			err = ERROR0 (ERR_INVALID_DATA, "EncodeExciteMOD: no triangles\n");
		FREE (positions);
		FREE (texcoords);
		FREE (corners);
		return err;
	}

	//--- sizes and layout ---------------------------------------------------

	const uint n_draws = (num_corn + 3 * 21845 - 1) / (3 * 21845);
	const uint dl_preamb = 18; // 3 CP writes
	const uint dl_len = dl_preamb + n_draws * 3 + num_corn * 2;
	uint geo_len = num_pos * 12 + num_tex * 4;
	geo_len = (geo_len + 31) & ~31u; // 32-byte align the display list
	const uint dl_off = 0x40 + geo_len;
	const uint dl_end = dl_off + dl_len;
	const uint dl_size = dl_len;

	u8 *buf = MALLOC (dl_end);
	memset (buf, 0, dl_end);
	memset (buf + 0x38, 0xe3, 8); // retail filler between header and arrays

	memcpy (buf, "3LDN", 4);
	const u32 hdr[14] = { 0, dl_end, 0x0e1e, 0x82, 0, 0, 0, num_pos, 12, 0, 0, 0, dl_size, 0 };
	for (int i = 1; i < 14; i++) // skip magic, written as bytes
		msh_u32le (buf + i * 4, hdr[i]);

	// bounding radius: max |v| over all unique positions
	double max_r2 = 0.0;
	for (uint i = 0; i < num_pos; i++)
	{
		const double x = positions[i].x, y = positions[i].y, z = positions[i].z;
		const double r2 = x * x + y * y + z * z;
		if (r2 > max_r2)
			max_r2 = r2;
	}
	msh_f32le (buf + 0x18, sqrt (max_r2));

	for (uint i = 0; i < num_pos; i++)
	{
		u8 *p = buf + 0x40 + i * 12;
		float fx = positions[i].x, fy = positions[i].y, fz = positions[i].z;
		u32 vx, vy, vz;
		memcpy (&vx, &fx, 4);
		memcpy (&vy, &fy, 4);
		memcpy (&vz, &fz, 4);
		mod_u32be (p, vx);
		mod_u32be (p + 4, vy);
		mod_u32be (p + 8, vz);
	}
	for (uint i = 0; i < num_tex; i++)
	{
		u8 *p = buf + 0x40 + num_pos * 12 + i * 4;
		for (uint c = 0; c < 2; c++)
		{
			// model_t parsed from COLLADA uses the conventional bottom-left T
			// origin; the GX display list uses top-left texture coordinates.
			const double d = c ? texcoords[i].v : texcoords[i].u;
			s32 scaled = (s32)lrint (d * 8192.0);
			if (scaled > 32767)
				scaled = 32767;
			if (scaled < -32768)
				scaled = -32768;
			mod_u16be (p + 2 * c, (u16)(s16)scaled);
		}
	}

	//--- display list --------------------------------------------------------

	u8 *dl = buf + dl_off;
	// VAT group A: XYZ f32 positions (shift 0), ST s16 texcoords (shift 13)
	const u32 vat_a = (1u << 0) | (4u << 1) | (0u << 4) | (1u << 21) | (3u << 22) | (13u << 25);
	mod_u32be (dl + 2, vat_a);
	mod_u32be (dl + 8, 0xc8241209u); // VAT B: retail constant
	mod_u32be (dl + 14, 0x04824120u); // VAT C: retail constant
	dl[0] = dl[6] = dl[12] = 0x08;
	dl[1] = 0x70;
	dl[7] = 0x80;
	dl[13] = 0x90;

	uint p = dl_preamb;
	for (uint c = 0; c < num_corn; c += 3 * 21845)
	{
		const uint n = num_corn - c < 3 * 21845 ? num_corn - c : 3 * 21845;
		dl[p++] = 0x90; // TRIANGLES, vertex format slot 0
		mod_u16be (dl + p, (u16)n);
		p += 2;
		for (uint k = 0; k < n; k++)
		{
			dl[p++] = (u8)corners[c + k].pos_idx;
			dl[p++] = (u8)corners[c + k].tex_idx;
		}
	}
	if (p != dl_len)
	{
		FREE (buf);
		FREE (corners);
		FREE (positions);
		FREE (texcoords);
		return ERROR0 (ERR_INTERNAL, "EncodeExciteMOD: internal DL size mismatch\n");
	}

	FREE (corners);
	FREE (positions);
	FREE (texcoords);

	const enumError rc = SaveFILE (out_path, 0, true, buf, dl_end, 0);
	FREE (buf);
	if (rc <= ERR_WARNING && verbose >= 0)
		fprintf (stdlog, "ENCODE MOD: -> %s (%u bytes, %u tris, %u pos, %u tex)\n", out_path,
			dl_end, num_corn / 3, num_pos, num_tex);
	return rc <= ERR_WARNING ? ERR_OK : rc;
}

//-----------------------------------------------------------------------------
///////////////		end of Excite Truck / ExciteBots	///////////////
