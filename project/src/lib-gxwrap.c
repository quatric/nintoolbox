#include "lib-gxwrap.h"
#include "lib-retro-txtr.h"
#include "lib-excite.h"

typedef struct gxw_t
{
	uint gx, w, h;
	size_t doff;
	size_t avail; // 0: up to the end of the file
	uint levels; // 0: as many as fit
	bool tga; // a Truevision TGA (RGBA <-> BGR(A)/gray), not GX tiles
	uint bpp;
} gxw_t;

static uint gxw_be16 (const u8 *p) { return p[0] << 8 | p[1]; }
static uint gxw_be32 (const u8 *p) { return (uint)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint gxw_le32 (const u8 *p) { return (uint)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }

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

static bool locate (const u8 *raw, size_t sz, ccp path, gxw_t *g)
{
	memset (g, 0, sizeof (*g));
	if (locate_tga (raw, sz, path, g))
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
