#include "lib-gxwrap.h"
#include "lib-retro-txtr.h"
#include "lib-excite.h"
#include "lib-lz10.h"

typedef struct gxw_t
{
	uint gx, w, h;
	size_t doff;
	size_t avail; // 0: up to the end of the file
	uint levels; // 0: as many as fit
	bool tga; // a Truevision TGA (RGBA <-> BGR(A)/gray), not GX tiles
	bool tklt; // Battle of Giants tile set ("TKLT" plain, "VNKT" LZ10 packed), palette from a TTLP file below a 2d folder
	bool nbfc; // DS raw tile dump (.nbfc / .char) edited through its sibling palette and map
	bool dst1; // DS "DST1" sprite sheet: RGB555 palette, 2/4/8 bit indices (doff = pixels, clut = palette)
	bool tim2; // a single-image PS2 TIM2 (tga fields: w, h, doff = pixels)
	uint bpp;
	uint itype, ctype, ncol;
	size_t clut;
} gxw_t;

static uint gxw_be16 (const u8 *p) { return p[0] << 8 | p[1]; }
static uint gxw_le16 (const u8 *p) { return p[0] | p[1] << 8; }
static uint gxw_be32 (const u8 *p) { return (uint)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint gxw_le32 (const u8 *p) { return (uint)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }

static u8 *load (ccp path, size_t *size);

static bool has_ext (ccp path, ccp ext)
{
	const size_t a = strlen (path), b = strlen (ext);
	return a > b && !strcasecmp (path + a - b, ext);
}

static void gx_block (uint gx, uint *bw, uint *bh, uint *bits)
{
	static const uint tab[15] = { 4, 8, 8, 16, 16, 16, 32, 0, 0, 0, 0, 0, 0, 0, 4 };
	*bits = tab[gx];
	*bw = gx == 0 || gx == 14 || gx == 1 || gx == 2 ? 8 : 4;
	*bh = gx == 0 || gx == 14 ? 8 : 4;
}

static bool gx_ok (uint gx) { return gx <= 6 || gx == 14; }

static size_t level_size (uint gx, uint w, uint h)
{
	uint bw, bh, bits;
	gx_block (gx, &bw, &bh, &bits);
	return (size_t)((w + bw - 1) / bw) * bw * ((h + bh - 1) / bh) * bh * bits / 8;
}

// Truevision TGA with a foreign extension, accepted like extract_tgaimg_file() does
static bool locate_tga (const u8 *raw, size_t sz, ccp path, gxw_t *g)
{
	if (has_ext (path, ".png") || has_ext (path, ".tga") || sz < 0x20 || raw[1]
		|| (raw[2] != 2 && raw[2] != 3 && raw[2] != 10 && raw[2] != 11) || raw[3] || raw[4]
		|| raw[5] || raw[6] || raw[7])
		return false;
	const uint w = raw[12] | raw[13] << 8, h = raw[14] | raw[15] << 8, bpp = raw[16], type = raw[2];
	if (!w || !h || w > 8192 || h > 8192 || (bpp != 8 && bpp != 24 && bpp != 32)
		|| (type == 2 || type == 10 ? bpp == 8 : bpp != 8))
		return false;
	const size_t npx = (size_t)w * h, bytes = bpp / 8, off = 18 + raw[0];
	if (type < 9)
	{
		if (off + npx * bytes > sz || sz - off - npx * bytes > 0x2000)
			return false;
	}
	else
	{
		size_t p = off, n = 0;
		while (n < npx && p < sz)
		{
			const uint c = raw[p++], run = (c & 0x7f) + 1;
			if (n + run > npx)
				break;
			p += c & 0x80 ? bytes : (size_t)run * bytes;
			if (p > sz)
				return false;
			n += run;
		}
		if (n != npx || sz - p > 0x2000)
			return false;
	}
	g->tga = true, g->w = w, g->h = h, g->bpp = bpp, g->doff = off;
	return true;
}

static bool locate_dst1 (const u8 *raw, size_t sz, gxw_t *g)
{
	if (sz < 0x14 || memcmp (raw, "DST1", 4))
		return false;
	const uint w = gxw_le16 (raw + 0xc), h = gxw_le16 (raw + 0xe), nc = gxw_le16 (raw + 0x10);
	const uint bpp = nc <= 4 ? 2 : nc <= 16 ? 4 : 8;
	const size_t o = 0x12 + 2 * (size_t)nc;
	if (!w || !h || !nc || nc > 256 || w * bpp % 8 || o + (size_t)w * h * bpp / 8 != sz)
		return false;
	g->dst1 = true, g->w = w, g->h = h, g->ncol = nc, g->bpp = bpp, g->doff = o, g->clut = 0x12;
	return true;
}

// PS2 TIM2 with one image (see extract_tim2png_file); only the top mip level is handled
static bool locate_tim2 (const u8 *raw, size_t sz, ccp path, gxw_t *g)
{
	if (!has_ext (path, ".tm2") || sz < 0x40 || memcmp (raw, "TIM2", 4) || gxw_le16 (raw + 6) != 1)
		return false;
	const u8 *h = raw + 0x10;
	const size_t total = gxw_le32 (h), clutsz = gxw_le32 (h + 4), imgsz = gxw_le32 (h + 8);
	const uint hs = gxw_le16 (h + 12), ncol = gxw_le16 (h + 14), ctype = h[18], itype = h[19];
	const uint w = gxw_le16 (h + 20), ht = gxw_le16 (h + 22);
	if (total < hs || 0x10 + total > sz || hs < 0x30 || !w || !ht || w > 4096 || ht > 4096 || itype < 1
		|| itype > 5)
		return false;
	const uint bpp = itype == 1 ? 16 : itype == 2 ? 24 : itype == 3 ? 32 : itype == 4 ? 4 : 8;
	const uint cbpp = (ctype & 3) == 1 ? 16 : (ctype & 3) == 2 ? 24 : (ctype & 3) == 3 ? 32 : 0;
	if ((u64)w * ht * bpp / 8 > imgsz || hs + imgsz + clutsz > total
		|| (itype >= 4 && (!cbpp || (u64)ncol * cbpp / 8 > clutsz)))
		return false;
	g->tim2 = true, g->w = w, g->h = ht, g->bpp = itype >= 4 ? cbpp : bpp, g->itype = itype;
	g->ctype = ctype, g->ncol = ncol, g->doff = 0x10 + hs, g->clut = 0x10 + hs + imgsz;
	return true;
}

// first image of a TPL starting at raw+toff
static bool locate_tpl (const u8 *raw, size_t sz, size_t toff, gxw_t *g)
{
	if (toff + 0x20 > sz || gxw_be32 (raw + toff) != 0x0020af30)
		return false;
	const size_t ih = toff + gxw_be32 (raw + toff + 0xc);
	if (ih + 0x24 > sz)
		return false;
	g->h = gxw_be16 (raw + ih), g->w = gxw_be16 (raw + ih + 2), g->gx = gxw_be32 (raw + ih + 4);
	g->doff = toff + gxw_be32 (raw + ih + 8);
	g->levels = raw[ih + 0x22] + 1u; // max_lod + 1
	return true;
}

// sibling file of a ".nbfc" ("foo.nbfp", "foo.NBFP"), loaded with the same lookup the decoder uses
static u8 *load_sibling (ccp path, ccp ext, size_t *sz)
{
	char buf[PATH_MAX];
	ccp dot = strrchr (path, '.');
	if (!dot || (size_t)(dot - path) + strlen (ext) >= sizeof (buf))
		return 0;
	for (uint up = 0; up < 2; up++)
	{
		snprintf (buf, sizeof (buf), "%.*s", (int)(dot - path), path);
		char *q = buf + strlen (buf);
		for (ccp e = ext; *e; e++)
			*q++ = up ? toupper ((u8)*e) : *e;
		*q = 0;
		u8 *d = load (buf, sz);
		if (d)
			return d;
	}
	return 0;
}

static bool locate_nbfc (const u8 *raw, size_t sz, ccp path, gxw_t *g)
{
	(void)raw;
	const bool ch = has_ext (path, ".char");
	if ((!ch && !has_ext (path, ".nbfc")) || sz < 32 || sz > 0x400000)
		return false;
	size_t psz;
	u8 *pal = load_sibling (path, ch ? ".pal" : ".nbfp", &psz);
	const bool ok = pal && psz >= 2 && psz <= 0x200 && !(psz & 1);
	FREE (pal);
	g->nbfc = ok;
	return ok;
}

static bool locate_tklt (const u8 *raw, size_t sz, gxw_t *g)
{
	const bool lz = sz >= 0x30 && !memcmp (raw, "VNKT", 4);
	if (sz < 0x30 || (!lz && memcmp (raw, "TKLT", 4)))
		return false;
	const size_t n4 = gxw_le16 (raw + 4), n8 = gxw_le16 (raw + 6), pl = gxw_le32 (raw + 0xd), data = 0x11 + pl;
	if (!(n4 + n8) || pl < 8 || pl > 200 || data + 4 > sz || (!lz && data + n4 * 32 + n8 * 64 != sz))
		return false;
	g->tklt = true, g->w = 128, g->h = (uint)((n4 + n8 + 15) / 16) * 8;
	return true;
}

static bool locate (const u8 *raw, size_t sz, ccp path, gxw_t *g)
{
	memset (g, 0, sizeof (*g));
	if (locate_tga (raw, sz, path, g) || locate_tim2 (raw, sz, path, g) || locate_dst1 (raw, sz, g) || locate_nbfc (raw, sz, path, g) || locate_tklt (raw, sz, g))
		return true;
	if (sz >= 0x40 && !memcmp (raw, "GCT0", 4))
	{
		g->gx = gxw_be32 (raw + 4), g->w = gxw_be16 (raw + 8), g->h = gxw_be16 (raw + 10), g->doff = 0x40;
	}
	else if (has_ext (path, ".wit") && sz >= 0x20)
	{
		for (uint hb = 0; hb < 8; hb += 4)
		{
			const uint w = gxw_le32 (raw + hb), h = gxw_le32 (raw + hb + 4), fmt = gxw_le32 (raw + hb + 8);
			const uint lv = gxw_le32 (raw + hb + 12);
			if (!w || !h || w > 4096 || h > 4096 || fmt > 14 || !gx_ok (fmt) || !lv || lv > 16)
				continue; // (the Cabela's CMPR variant with fmt 0x800 has two chains: not re-encoded)
			g->gx = fmt, g->w = w, g->h = h, g->doff = hb ? 0x18 : 0x10, g->levels = lv;
			break;
		}
	}
	else if (has_ext (path, ".ctx") && sz >= 0x50 && !gxw_be32 (raw) && gxw_be32 (raw + 12) == 0x34)
	{
		g->w = gxw_be16 (raw + 4), g->h = gxw_be16 (raw + 6), g->gx = gxw_be16 (raw + 8), g->doff = 0x40;
		g->levels = gxw_be16 (raw + 10);
		if (!g->levels || g->levels > 16)
			return false;
	}
	else if (has_ext (path, ".gaftx") && sz >= 0x44)
	{
		const uint fmt = gxw_be32 (raw + 0xc);
		g->w = gxw_be16 (raw), g->h = gxw_be16 (raw + 2), g->doff = gxw_be32 (raw + 0x24);
		g->gx = fmt == 18 ? 14 : fmt == 8 ? 6 : fmt == 9 ? 3 : fmt == 11 ? 4 : 99;
		g->levels = gxw_be32 (raw + 0x38);
		if (gxw_be16 (raw + 4) != g->w || gxw_be16 (raw + 6) != g->h || gxw_be32 (raw + 8) != 0x10001)
			return false;
	}
	else if (has_ext (path, ".s3t") && sz >= 0x40 && !gxw_be32 (raw + 12) && !gxw_be32 (raw + 16))
		g->gx = gxw_be32 (raw), g->w = gxw_be32 (raw + 4), g->h = gxw_be32 (raw + 8), g->doff = 0x20, g->levels = 1;
	else if (has_ext (path, ".5a3") && sz == 192 * 64 * 2)
		g->gx = 5, g->w = 192, g->h = 64, g->doff = 0, g->levels = 1; // headerless Wii Chess save banner
	else if (has_ext (path, ".iwi") && sz > 0x28 && !memcmp (raw, "IWi", 3) && (raw[3] == 11 || raw[3] == 7)
		&& raw[4] == 11)
	{
		g->gx = 14, g->w = raw[6] | raw[7] << 8, g->h = raw[8] | raw[9] << 8, g->doff = raw[3] == 7 ? 0x1c : 0x20;
		g->levels = 1;
		if (!g->w || !g->h || g->w > 4096 || g->h > 4096
			|| sz - g->doff != (size_t)((g->w + 7) / 8 * 8) * ((g->h + 7) / 8 * 8) / 2)
			return false;
	}
	else if (has_ext (path, ".a2") && sz >= 0x30 && !gxw_be32 (raw + 16) && !gxw_be32 (raw + 20))
	{
		g->gx = gxw_be32 (raw), g->w = gxw_be32 (raw + 4), g->h = gxw_be32 (raw + 8), g->doff = 0x1c;
		g->levels = gxw_be32 (raw + 12);
		if (!g->levels || g->levels > 16)
			return false;
	}
	else if (has_ext (path, ".bct") && sz >= 0x50 && raw[0] == 6)
	{
		g->w = gxw_be16 (raw + 4), g->h = gxw_be16 (raw + 6), g->gx = 14, g->doff = 0x30;
		g->avail = gxw_be32 (raw + 0x1c);
		if ((g->w & 7) || (g->h & 7))
			return false;
	}
	else if (has_ext (path, ".txw") && sz >= 0x80 && gxw_be32 (raw) == 0xafaf0002
		&& gxw_be32 (raw + 0x0c) == 0xbeefface)
	{
		const uint type = raw[0x12] << 8 | raw[0x13];
		g->w = raw[0x14] << 8 | raw[0x15], g->h = raw[0x16] << 8 | raw[0x17], g->gx = 14, g->doff = 0x40;
		g->levels = 1;
		if (type != 2 && type != 4)
			return false;
	}
	else if (has_ext (path, ".tgs") && sz >= 0x60 && !memcmp (raw, "tgs\0", 4))
	{
		if (!locate_tpl (raw, sz, gxw_be32 (raw + 0x20), g))
			return false;
	}
	else if (has_ext (path, ".pix") && sz >= 0x100 && !memcmp (raw, "FPIX", 4))
	{
		size_t o = 0;
		for (size_t q = 0x10; q + 0x20 < sz && q < 0x400 && !o; q += 4)
			if (gxw_be32 (raw + q) == 0x0020af30)
				o = q;
		if (!o || !locate_tpl (raw, sz, o, g))
			return false;
	}
	else
		return false;

	if (!g->avail || g->avail > sz - g->doff)
		g->avail = g->doff < sz ? sz - g->doff : 0;
	return g->gx < 15 && gx_ok (g->gx) && g->w && g->h && g->w <= 4096 && g->h <= 4096
		&& g->doff < sz && level_size (g->gx, g->w, g->h) <= g->avail;
}

static u8 *load (ccp path, size_t *size)
{
	*size = 0;
	FILE *f = fopen (path, "rb");
	if (!f)
		return 0;
	u8 *buf = 0;
	if (!fseeko (f, 0, SEEK_END))
	{
		const off_t n = ftello (f);
		if (n > 0 && n <= 0x8000000 && !fseeko (f, 0, SEEK_SET) && (buf = MALLOC ((size_t)n)))
		{
			if (fread (buf, 1, (size_t)n, f) == (size_t)n)
				*size = (size_t)n;
			else
				FREE (buf), buf = 0;
		}
	}
	fclose (f);
	return buf;
}

bool GXWrapIsFile (ccp path)
{
	size_t sz;
	u8 *raw = load (path, &sz);
	gxw_t g;
	const bool ok = raw && locate (raw, sz, path, &g);
	FREE (raw);
	return ok;
}

// encode the base level and then every box-filtered level that still fits into avail bytes
static void encode_chain (const gxw_t *g, const u8 *rgba, u8 *out, size_t avail)
{
	uint w = g->w, h = g->h;
	u8 *cur = MALLOC ((size_t)w * h * 4);
	if (!cur)
		return;
	memcpy (cur, rgba, (size_t)w * h * 4);
	for (uint level = 0; !g->levels || level < g->levels; level++)
	{
		const size_t n = level_size (g->gx, w, h);
		if (n > avail)
			break;
		GXEncodeRGBA (g->gx, w, h, cur, out);
		out += n, avail -= n;
		if (w == 1 && h == 1)
			break;
		const uint nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
		u8 *next = MALLOC ((size_t)nw * nh * 4);
		if (!next)
			break;
		for (uint y = 0; y < nh; y++)
			for (uint x = 0; x < nw; x++)
				for (uint c = 0; c < 4; c++)
				{
					uint sum = 0, cnt = 0;
					for (uint dy = 0; dy < (h > 1 ? 2u : 1u); dy++)
						for (uint dx = 0; dx < (w > 1 ? 2u : 1u); dx++, cnt++)
							sum += cur[((size_t)(y * 2 + dy) * w + x * 2 + dx) * 4 + c];
					next[((size_t)y * nw + x) * 4 + c] = (u8)((sum + cnt / 2) / cnt);
				}
		FREE (cur);
		cur = next, w = nw, h = nh;
	}
	FREE (cur);
}

static enumError store (ccp path, const u8 *data, size_t size)
{
	char tmp[PATH_MAX];
	snprintf (tmp, sizeof (tmp), "%s.gxw-tmp", path);
	FILE *f = fopen (tmp, "wb");
	if (!f)
		return ERR_CANT_CREATE;
	const bool bad = fwrite (data, 1, size, f) != size;
	if (fclose (f) || bad || rename (tmp, path))
	{
		unlink (tmp);
		return ERR_WRITE_FAILED;
	}
	return ERR_OK;
}

static u8 tim2_alpha (uint a, uint bits) { return bits == 1 ? (a >= 128) : a >= 128 ? 0x80 : a / 2; }

// direct colour types are re-encoded; indexed types pick the nearest entry of the file's own CLUT
static enumError reinject_tim2 (const Image_t *img, u8 *raw, const gxw_t *g)
{
	u8 *pix = raw + g->doff;
	for (uint y = 0; y < g->h; y++)
		for (uint x = 0; x < g->w; x++)
		{
			const uint i = y * g->w + x;
			const u8 *p = img->data + 4 * ((size_t)y * img->xwidth + x);
			if (g->itype < 4)
			{
				u8 *o = pix + (size_t)i * g->bpp / 8;
				if (g->bpp == 16)
				{
					const uint v = (p[0] * 31 + 127) / 255 | (p[1] * 31 + 127) / 255 << 5
						| (p[2] * 31 + 127) / 255 << 10 | tim2_alpha (p[3], 1) << 15;
					o[0] = v, o[1] = v >> 8;
				}
				else
				{
					o[0] = p[0], o[1] = p[1], o[2] = p[2];
					if (g->bpp == 32)
						o[3] = tim2_alpha (p[3], 8);
				}
				continue;
			}
			uint best = 0, best_d = ~0u;
			const uint cb = g->bpp / 8;
			for (uint c = 0; c < g->ncol && best_d; c++)
			{
				const u8 *e = raw + g->clut + (size_t)c * g->bpp / 8; // physical CLUT position
				u8 col[4] = { 0, 0, 0, 255 };
				if (g->bpp == 16)
				{
					const uint v = e[0] | e[1] << 8;
					col[0] = (v & 31) * 255 / 31, col[1] = (v >> 5 & 31) * 255 / 31;
					col[2] = (v >> 10 & 31) * 255 / 31, col[3] = v & 0x8000 ? 255 : 0;
				}
				else
				{
					col[0] = e[0], col[1] = e[1], col[2] = e[2];
					if (cb == 4)
						col[3] = e[3] >= 0x80 ? 255 : e[3] * 2;
				}
				uint d = 0;
				for (uint k = 0; k < 4; k++)
					d += (uint)((int)col[k] - (int)p[k]) * (uint)((int)col[k] - (int)p[k]);
				if (d < best_d)
					best_d = d, best = c;
			}
			// 8 bit CLUTs are block swizzled (swapping bits 3 and 4 is its own inverse)
			uint store_idx = best;
			if (g->itype == 5 && !(g->ctype & 0x80) && g->ncol >= 256)
				store_idx = (best & 0x18) == 8 || (best & 0x18) == 0x10 ? best ^ 0x18 : best;
			if (g->itype == 4)
				pix[i >> 1] = (u8)((pix[i >> 1] & (i & 1 ? 0x0f : 0xf0)) | store_idx << ((i & 1) * 4));
			else
				pix[i] = (u8)store_idx;
		}
	return ERR_OK;
}

// index 0 is the transparent key; other pixels take the nearest palette colour
static void reinject_dst1 (const Image_t *img, u8 *raw, const gxw_t *g)
{
	u8 pal[256][3];
	for (uint c = 0; c < g->ncol; c++)
	{
		const uint v = gxw_le16 (raw + g->clut + 2 * c);
		pal[c][0] = (v & 31) * 255 / 31, pal[c][1] = (v >> 5 & 31) * 255 / 31, pal[c][2] = (v >> 10 & 31) * 255 / 31;
	}
	u8 *pix = raw + g->doff;
	memset (pix, 0, (size_t)g->w * g->h * g->bpp / 8);
	for (uint y = 0; y < g->h; y++)
		for (uint x = 0; x < g->w; x++)
		{
			const u8 *p = img->data + 4 * ((size_t)y * img->xwidth + x);
			uint best = 0, best_d = ~0u;
			for (uint c = 1; p[3] >= 128 && c < g->ncol; c++)
			{
				uint d = 0;
				for (uint k = 0; k < 3; k++)
					d += (uint)(((int)pal[c][k] - p[k]) * ((int)pal[c][k] - p[k]));
				if (d < best_d)
					best_d = d, best = c;
			}
			const size_t bit = ((size_t)y * g->w + x) * g->bpp;
			pix[bit / 8] |= (u8)(best << (bit & 7));
		}
}

static uint nbf_c555 (const u8 *p) { uint c = gxw_le16 (p); return (c & 31) | (c >> 5 & 31) << 8 | (c >> 10 & 31) << 16; }

// Mirror of the decoder (extract/nbfraw.inc): every map cell (or tile, without a map) owns 8x8 pixels of the
// sheet; each pixel becomes the nearest colour of its palette bank. Tiles shared by several cells take the last one.
static enumError reinject_nbfc (const Image_t *img, u8 *raw, size_t sz, ccp path)
{
	const bool ch = has_ext (path, ".char");
	size_t psz = 0, msz = 0;
	u8 *pal = load_sibling (path, ch ? ".pal" : ".nbfp", &psz);
	u8 *map = load_sibling (path, ch ? ".scr" : ".nbfs", &msz);
	enumError e = ERR_OK;
	if (!pal)
		return ERR_NOTHING_TO_DO;
	if (map && msz >= 4 && !memcmp (map, "VSCR", 4))
		FREE (map), map = 0, msz = 0;
	const uint ncol = (uint)psz / 2, nent = (uint)msz / 2;
	bool banks = false;
	for (uint i = 0; i < nent; i++)
		banks |= gxw_le16 (map + 2 * i) >> 12 != 0;
	const uint bpp = ncol <= 16 || banks ? 4 : 8, tb = bpp * 8;
	uint wt, ht;
	if (nent >= 16 && nent <= 4096)
	{
		wt = nent % 32 == 0 ? 32 : nent % 16 == 0 ? 16 : 0;
		ht = wt ? nent / wt : 0;
	}
	else
		FREE (map), map = 0, wt = 32, ht = ((uint)(sz / tb) + 31) / 32;
	if (!ht || ht > 128)
		e = ERR_NOTHING_TO_DO;
	else if (img->width != wt * 8 || img->height != ht * 8)
		e = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the tile sheet is %ux%u: %s\n", img->width,
			img->height, wt * 8, ht * 8, path);
	for (uint i = 0; !e && i < wt * ht; i++)
	{
		const uint en = map ? gxw_le16 (map + 2 * i) : i, t = map ? en & 0x3ff : i, hf = map ? en >> 10 & 1 : 0,
			vf = map ? en >> 11 & 1 : 0, bk = map ? en >> 12 : 0;
		for (uint y = 0; y < 8; y++)
			for (uint x = 0; x < 8; x++)
			{
				const uint sy = vf ? 7 - y : y, sx = hf ? 7 - x : x;
				const size_t o = (size_t)t * tb + (bpp == 4 ? sy * 4 + sx / 2 : sy * 8 + sx);
				if (o >= sz)
					continue;
				const u8 *p = img->data + 4 * ((size_t)((i / wt) * 8 + y) * img->xwidth + (i % wt) * 8 + x);
				uint best = 0, best_d = ~0u;
				for (uint v = 0; v < (bpp == 4 ? 16u : 256u); v++)
				{
					uint ci = v + (bpp == 4 ? bk * 16 : 0);
					if (ci >= ncol)
						ci = v;
					if (ci >= ncol)
						continue;
					const uint c = nbf_c555 (pal + 2 * ci);
					uint d = 0;
					for (uint k = 0; k < 3; k++)
					{
						const int dv = (int)((c >> (8 * k) & 31) * 255 / 31) - p[k];
						d += (uint)(dv * dv);
					}
					if (d < best_d)
						best_d = d, best = v;
				}
				if (bpp == 4)
					raw[o] = (u8)(sx & 1 ? (raw[o] & 15) | best << 4 : (raw[o] & 0xf0) | best);
				else
					raw[o] = (u8)best;
			}
	}
	FREE (pal);
	FREE (map);
	return e;
}

// Tile 0 and every transparent pixel use index 0; 4 bit tiles are drawn with palette bank 0, 8 bit tiles with the whole palette.
// VNKT is rebuilt around a fresh LZ10 stream, TKLT is patched in place; either way the palette file stays as it is.
static enumError reinject_tklt (const Image_t *img, const u8 *raw, size_t sz, ccp path, u8 **out, size_t *outn)
{
	const bool lz = !memcmp (raw, "VNKT", 4);
	const size_t n4 = gxw_le16 (raw + 4), n8 = gxw_le16 (raw + 6), pl = gxw_le32 (raw + 0xd), data = 0x11 + pl, bytes = n4 * 32 + n8 * 64;
	ccp r = !strncmp (path, "2d/", 3) ? path - 1 : strstr (path, "/2d/");
	if (!r)
		return ERR_NOTHING_TO_DO;
	char pp[PATH_MAX];
	snprintf (pp, sizeof (pp), "%.*s%s%.*s", r < path ? 0 : (int)(r - path), path, r < path ? "" : "/", (int)pl, (ccp)raw + 0x11);
	size_t psz;
	u8 *pal = load (pp, &psz);
	if (!pal || psz < 10 || memcmp (pal, "TTLP", 4))
	{
		FREE (pal);
		return ERR_NOTHING_TO_DO;
	}
	uint ncol = gxw_le16 (pal + 4) / 2;
	if (ncol > (psz - 8) / 2)
		ncol = (uint)(psz - 8) / 2;
	u8 *t = CALLOC (1, bytes);
	if (!t)
		return FREE (pal), ERR_OUT_OF_MEMORY;
	for (size_t i = 0; i < n4 + n8; i++)
	{
		const bool b8 = i >= n4;
		u8 *tile = b8 ? t + n4 * 32 + (i - n4) * 64 : t + i * 32;
		for (uint y = 0; y < 8; y++)
			for (uint x = 0; x < 8; x++)
			{
				const u8 *p = img->data + 4 * ((size_t)((i / 16) * 8 + y) * img->xwidth + (i % 16) * 8 + x);
				uint best = 0, best_d = ~0u;
				for (uint c = 1; p[3] >= 128 && c < (b8 ? 256u : 16u) && c < ncol; c++)
				{
					const uint v = gxw_le16 (pal + 8 + 2 * c);
					uint d = 0;
					for (uint k = 0; k < 3; k++)
					{
						const int dv = (int)((v >> (5 * k) & 31) * 255 / 31) - p[k];
						d += (uint)(dv * dv);
					}
					if (d < best_d)
						best_d = d, best = c;
				}
				if (b8)
					tile[y * 8 + x] = (u8)best;
				else
					tile[y * 4 + x / 2] |= (u8)(x & 1 ? best << 4 : best);
			}
	}
	FREE (pal);
	enumError e = ERR_OK;
	if (!lz)
	{
		*out = MALLOC (sz), *outn = sz;
		if (*out)
			memcpy (*out, raw, data), memcpy (*out + data, t, bytes);
		else
			e = ERR_OUT_OF_MEMORY;
	}
	else
	{
		u8 *z = 0;
		uint zn = 0;
		e = EncodeLZ10LZ11 (&z, &zn, t, (uint)bytes, false);
		if (!e && (*out = MALLOC (data + 4 + zn)))
		{
			memcpy (*out, raw, data);
			memcpy (*out + data, &(u8[4]){ (u8)zn, (u8)(zn >> 8), (u8)(zn >> 16), (u8)(zn >> 24) }, 4);
			memcpy (*out + data + 4, z, zn);
			*outn = data + 4 + zn;
		}
		else if (!e)
			e = ERR_OUT_OF_MEMORY;
		FREE (z);
	}
	FREE (t);
	return e;
}

enumError GXWrapReinject (Image_t *img, ccp dest)
{
	size_t sz;
	u8 *raw = load (dest, &sz);
	gxw_t g;
	if (!raw || !locate (raw, sz, dest, &g))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (g.tklt)
	{
		enumError e = ERR_OK;
		u8 *out = 0;
		size_t outn = 0;
		Transform2XIMG (img);
		if (img->iform != IMG_X_RGB)
			e = ERR_NOTHING_TO_DO;
		else if (img->width != g.w || img->height != g.h)
			e = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the tile sheet is %ux%u: %s\n", img->width,
				img->height, g.w, g.h, dest);
		else
			e = reinject_tklt (img, raw, sz, dest, &out, &outn);
		if (!e)
			e = store (dest, out, outn);
		FREE (out);
		FREE (raw);
		return e;
	}
	if (g.nbfc)
	{
		enumError e = ERR_OK;
		Transform2XIMG (img);
		if (img->iform != IMG_X_RGB)
			e = ERR_NOTHING_TO_DO;
		else
			e = reinject_nbfc (img, raw, sz, dest);
		if (!e)
			e = store (dest, raw, sz);
		FREE (raw);
		return e;
	}
	if (g.tim2 || g.dst1)
	{
		enumError e = ERR_OK;
		Transform2XIMG (img);
		if (img->iform != IMG_X_RGB)
			e = ERR_NOTHING_TO_DO;
		else if (img->width != g.w || img->height != g.h)
			e = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the image is %ux%u: %s\n", img->width,
				img->height, g.w, g.h, dest);
		else if (g.dst1)
			reinject_dst1 (img, raw, &g);
		else
			e = reinject_tim2 (img, raw, &g);
		if (!e)
			e = store (dest, raw, sz);
		FREE (raw);
		return e;
	}
	if (g.tga)
	{
		enumError e = ERR_OK;
		Transform2XIMG (img);
		if (img->iform != IMG_X_RGB)
			e = ERR_NOTHING_TO_DO;
		else if (img->width != g.w || img->height != g.h)
			e = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the TGA is %ux%u: %s\n", img->width,
				img->height, g.w, g.h, dest);
		else
		{
			// header and image id are kept; pixels are written uncompressed (type 2 / 3)
			const uint bytes = g.bpp / 8;
			const size_t out_size = g.doff + (size_t)g.w * g.h * bytes;
			u8 *out = CALLOC (1, out_size);
			if (!out)
				e = ERR_OUT_OF_MEMORY;
			else
			{
				memcpy (out, raw, g.doff);
				out[2] = bytes == 1 ? 3 : 2;
				for (uint y = 0; y < g.h; y++)
				{
					const uint dy = (raw[17] & 0x20) ? y : g.h - 1 - y;
					for (uint x = 0; x < g.w; x++)
					{
						const u8 *p = img->data + 4 * ((size_t)y * img->xwidth + x);
						u8 *o = out + g.doff + ((size_t)dy * g.w + x) * bytes;
						if (bytes == 1)
							o[0] = (u8)((p[0] + p[1] + p[2]) / 3);
						else
						{
							o[0] = p[2], o[1] = p[1], o[2] = p[0];
							if (bytes == 4)
								o[3] = p[3];
						}
					}
				}
				e = store (dest, out, out_size);
				FREE (out);
			}
		}
		FREE (raw);
		return e;
	}
	Transform2XIMG (img);
	enumError err = ERR_OK;
	if (img->iform != IMG_X_RGB)
		err = ERR_NOTHING_TO_DO;
	else if (img->width != g.w || img->height != g.h)
		err = ERROR0 (ERR_INVALID_IFORM, "PNG is %ux%u but the texture is %ux%u: %s\n", img->width,
			img->height, g.w, g.h, dest);
	else
	{
		u8 *rgba = MALLOC ((size_t)g.w * g.h * 4);
		if (!rgba)
			err = ERR_OUT_OF_MEMORY;
		else
		{
			for (uint y = 0; y < g.h; y++)
				memcpy (rgba + 4 * (size_t)y * g.w, img->data + 4 * (size_t)y * img->xwidth, 4 * g.w);
			encode_chain (&g, rgba, raw + g.doff, g.avail);
			FREE (rgba);
			err = store (dest, raw, sz);
		}
	}
	FREE (raw);
	return err;
}

enumError GXWrapSyncFromTPL (ccp tpl_path)
{
	char wrap[PATH_MAX];
	snprintf (wrap, sizeof (wrap), "%s", tpl_path);
	if (!has_ext (wrap, ".tpl"))
		return ERR_NOTHING_TO_DO;
	wrap[strlen (wrap) - 4] = 0;
	size_t wsz, tsz;
	u8 *w = load (wrap, &wsz), *t = load (tpl_path, &tsz);
	gxw_t g;
	enumError err = ERR_NOTHING_TO_DO;
	if (w && t && tsz > 0x20 && gxw_be32 (t) == 0x0020af30 && locate (w, wsz, wrap, &g) && !g.tga)
	{
		const uint ih = gxw_be32 (t + 0xc), n = level_size (g.gx, g.w, g.h);
		const size_t doff = ih && ih + 0xc <= tsz ? gxw_be32 (t + ih + 8) : 0;
		if (ih && ih + 0xc <= tsz && gxw_be32 (t + ih + 4) == g.gx && doff && doff + n <= tsz
			&& gxw_be16 (t + ih) == g.h && gxw_be16 (t + ih + 2) == g.w)
		{
			// regenerate the mip chain from the new base level, then keep the base bytes exactly
			u8 *rgba = 0;
			if (!DecodeGXTexture_RGBA (&rgba, g.w, g.h, g.gx, t + doff, n, 0, 0, 0) && rgba)
				encode_chain (&g, rgba, w + g.doff, g.avail);
			FREE (rgba);
			memcpy (w + g.doff, t + doff, n);
			err = store (wrap, w, wsz);
		}
	}
	FREE (w);
	FREE (t);
	return err;
}
