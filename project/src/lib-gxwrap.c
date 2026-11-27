#include "lib-gxwrap.h"
#include "lib-retro-txtr.h"

typedef struct gxw_t
{
	uint gx, w, h;
	size_t doff;
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

static bool locate (const u8 *raw, size_t sz, ccp path, gxw_t *g)
{
	memset (g, 0, sizeof (*g));
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
			g->gx = fmt, g->w = w, g->h = h, g->doff = hb ? 0x18 : 0x10;
			break;
		}
	}
	else if (has_ext (path, ".ctx") && sz >= 0x50 && !gxw_be32 (raw) && gxw_be32 (raw + 12) == 0x34)
	{
		g->w = gxw_be16 (raw + 4), g->h = gxw_be16 (raw + 6), g->gx = gxw_be16 (raw + 8), g->doff = 0x40;
		const uint lv = gxw_be16 (raw + 10);
		if (!lv || lv > 16)
			return false;
	}
	else if (has_ext (path, ".gaftx") && sz >= 0x44)
	{
		const uint fmt = gxw_be32 (raw + 0xc);
		g->w = gxw_be16 (raw), g->h = gxw_be16 (raw + 2), g->doff = gxw_be32 (raw + 0x24);
		g->gx = fmt == 18 ? 14 : fmt == 8 ? 6 : fmt == 9 ? 3 : fmt == 11 ? 4 : 99;
		if (gxw_be16 (raw + 4) != g->w || gxw_be16 (raw + 6) != g->h || gxw_be32 (raw + 8) != 0x10001)
			return false;
	}
	else if (has_ext (path, ".s3t") && sz >= 0x40 && !gxw_be32 (raw + 12) && !gxw_be32 (raw + 16))
		g->gx = gxw_be32 (raw), g->w = gxw_be32 (raw + 4), g->h = gxw_be32 (raw + 8), g->doff = 0x20;
	else
		return false;

	return g->gx < 15 && gx_ok (g->gx) && g->w && g->h && g->w <= 4096 && g->h <= 4096
		&& g->doff < sz && g->doff + level_size (g->gx, g->w, g->h) <= sz;
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
	for (;;)
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
			encode_chain (&g, rgba, raw + g.doff, sz - g.doff);
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
	if (w && t && tsz > 0x20 && gxw_be32 (t) == 0x0020af30 && locate (w, wsz, wrap, &g))
	{
		const uint ih = gxw_be32 (t + 0xc), n = level_size (g.gx, g.w, g.h);
		const size_t doff = ih && ih + 0xc <= tsz ? gxw_be32 (t + ih + 8) : 0;
		if (ih && ih + 0xc <= tsz && gxw_be32 (t + ih + 4) == g.gx && doff && doff + n <= tsz
			&& gxw_be16 (t + ih) == g.h && gxw_be16 (t + ih + 2) == g.w)
		{
			memcpy (w + g.doff, t + doff, n);
			err = store (wrap, w, wsz);
		}
	}
	FREE (w);
	FREE (t);
	return err;
}
