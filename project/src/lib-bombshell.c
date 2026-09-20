// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// BombShell engine data pack (.xwi / .xdx9) scanner; see lib-bombshell.h.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-bombshell.h"
#include "lib-bntx.h"
#include "lib-excite.h"

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
	if (o + 4 > c->size)
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
static void bs_unique (bombshell_asset_t *list, uint n)
{
	bombshell_asset_t *a = list + n;
	char base[sizeof (a->name)];
	snprintf (base, sizeof (base), "%s", a->name);
	for (uint k = 2;; k++)
	{
		bool clash = false;
		for (uint i = 0; i < n && !clash; i++)
			clash = list[i].kind == a->kind && list[i].dir == a->dir && !strcasecmp (list[i].name, a->name);
		if (!clash)
			return;
		snprintf (a->name, sizeof (a->name), "%.80s_%u", base, k);
	}
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
