// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Torus Games hunkfiles; see lib-torus.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-torus.h"
#include "lib-excite.h"
#include "lib-dspadpcm.h"
#include <string.h>

static u32 tr_rd32 (const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static u32 tr_rd16 (const u8 *p) { return p[0] | p[1] << 8; }
static u32 tr_be32 (const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static u32 tr_be16 (const u8 *p) { return p[0] << 8 | p[1]; }

bool IsTorusHnk (const u8 *d, size_t size)
{
	if (size < 16 || (tr_rd16 (d + 4) & 0xfff) != 0x70)
		return false;
	size_t p = 0;
	while (p + 8 <= size)
		p += 8 + (size_t)tr_rd32 (d + p);
	return p == size;
}

// Size of the GX mip chain (or just the base level for MIPS<=1).
static size_t tr_chain (uint w, uint h, uint bpp, uint bw, uint bh, uint mips)
{
	size_t total = 0;
	for (uint m = 0; m < (mips ? mips : 1); m++)
	{
		total += (size_t)((w + bw - 1) / bw * bw) * ((h + bh - 1) / bh * bh) * bpp / 8;
		w = w > 1 ? w / 2 : 1;
		h = h > 1 ? h / 2 : 1;
	}
	return total;
}

static bool tr_names (const u8 *pl, size_t n, char *cls, size_t csz, char *name, size_t nsz)
{
	if (n < 12 || tr_rd16 (pl + 4) < 2)
		return false;
	const uint l0 = tr_rd16 (pl + 6), l1 = tr_rd16 (pl + 8);
	if (!l0 || !l1 || 10 + l0 + l1 > n || pl[9 + l0] || pl[9 + l0 + l1])
		return false;
	snprintf (cls, csz, "%s", (ccp)pl + 10);
	snprintf (name, nsz, "%s", (ccp)pl + 10 + l0);
	return true;
}

torus_asset_t *ListTorusHnk (const u8 *d, size_t size, uint *count)
{
	*count = 0;
	uint cap = 64;
	torus_asset_t *list = MALLOC (cap * sizeof (*list));
	if (!list)
		return 0;
	size_t p = 0;
	while (p + 8 <= size)
	{
		const size_t sz = tr_rd32 (d + p);
		const uint kind = tr_rd16 (d + p + 4) & 0xfff;
		const u8 *pl = d + p + 8;
		if (p + 8 + sz > size)
			break;
		char cls[64], name[128];
		if (kind == 0x71 && tr_names (pl, sz, cls, sizeof (cls), name, sizeof (name)))
		{
			// the two records after the name chunk
			const size_t q = p + 8 + sz;
			if (q + 8 > size)
				break;
			const size_t s1 = tr_rd32 (d + q), q2 = q + 8 + s1;
			const uint k1 = tr_rd16 (d + q + 4) & 0xfff;
			if (q2 + 8 > size || q + 8 + s1 > size)
			{
				p += 8 + sz;
				continue;
			}
			const size_t s2 = tr_rd32 (d + q2);
			const uint k2 = tr_rd16 (d + q2 + 4) & 0xfff;
			const u8 *b1 = d + q + 8, *b2 = d + q2 + 8;
			torus_asset_t a;
			memset (&a, 0, sizeof (a));
			bool ok = false;
			if (!strcmp (cls, "TSETexture") && k1 == 0x150 && k2 == 0x151 && s1 >= 32
				&& q2 + 8 + s2 <= size)
			{
				a.kind = TORUS_TEXTURE;
				a.width = tr_be16 (b1 + 12);
				a.height = tr_be16 (b1 + 14);
				const uint mips = b1[26];
				a.pixels = b2;
				a.pixel_size = s2;
				if (a.width && a.height && a.width <= 4096 && a.height <= 4096)
				{
					if (s2 == tr_chain (a.width, a.height, 32, 4, 4, mips))
						a.gx_format = 6, ok = true;
					else if (s2 == tr_chain (a.width, a.height, 4, 8, 8, mips))
						a.gx_format = 14, ok = true;
					else if (s2 == tr_chain (a.width, a.height, 8, 8, 4, mips))
						a.gx_format = 1, ok = true;
				}
			}
			else if (!strcmp (cls, "SqueakStream") && (k1 == 0x92) && k2 == 0x93 && s1 >= 100
				&& q2 + 8 + s2 <= size && !memcmp (b1, "IWAR", 4))
			{
				a.kind = TORUS_STREAM;
				a.channels = b1[6];
				a.samples = tr_be32 (b1 + 8);
				a.rate = tr_be32 (b1 + 12);
				a.header = b1;
				a.header_size = s1;
				const uint skip = 4 * (a.channels ? a.channels : 1);
				if (a.channels >= 1 && a.channels <= 2 && s2 > skip && s1 >= 64u + 48u * a.channels - 16u
					&& a.samples)
				{
					snprintf (a.raw, sizeof (a.raw), "%.*s", (int)(s2 - skip), (ccp)b2 + skip);
					ok = a.raw[0] != 0;
				}
			}
			if (ok)
			{
				snprintf (a.name, sizeof (a.name), "%s", name);
				if (*count == cap)
				{
					cap *= 2;
					torus_asset_t *n = REALLOC (list, cap * sizeof (*list));
					if (!n)
						break;
					list = n;
				}
				list[(*count)++] = a;
			}
		}
		p += 8 + sz;
	}
	return list;
}

enumError DecodeTorusTexture (u8 **rgba, const torus_asset_t *a)
{
	if (a->kind != TORUS_TEXTURE)
		return ERR_INVALID_DATA;
	return DecodeGXTexture_RGBA (rgba, a->width, a->height, a->gx_format, a->pixels,
		a->pixel_size > 0xffffffffu ? 0xffffffffu : (uint)a->pixel_size, 0, 0, 0);
}

static void tr_put32 (u8 *p, u32 v) { p[0] = v, p[1] = v >> 8, p[2] = v >> 16, p[3] = v >> 24; }

enumError DecodeTorusStream (u8 **wav, size_t *wav_size, const torus_asset_t *a, const u8 *raw, size_t raw_size)
{
	if (a->kind != TORUS_STREAM || !a->channels || a->channels > 2 || !a->samples)
		return ERR_INVALID_DATA;
	const uint ch = a->channels;
	const size_t need = (size_t)DspAdpcmByteCount (a->samples);
	const size_t chsize = ch == 2 ? raw_size / 2 : raw_size;
	if (chsize < need)
		return ERR_INVALID_DATA;
	const size_t bytes = 44 + (size_t)a->samples * 2 * ch;
	u8 *w = MALLOC (bytes);
	if (!w)
		return ERR_OUT_OF_MEMORY;
	memcpy (w, "RIFF", 4);
	tr_put32 (w + 4, (u32)(bytes - 8));
	memcpy (w + 8, "WAVEfmt ", 8);
	tr_put32 (w + 16, 16);
	w[20] = 1, w[21] = 0, w[22] = ch, w[23] = 0;
	tr_put32 (w + 24, a->rate);
	tr_put32 (w + 28, a->rate * 2 * ch);
	w[32] = 2 * ch, w[33] = 0, w[34] = 16, w[35] = 0;
	memcpy (w + 36, "data", 4);
	tr_put32 (w + 40, a->samples * 2 * ch);
	for (uint c = 0; c < ch; c++)
	{
		const u8 *h = a->header + 64 + 48 * c;
		s16 coefs[16];
		for (uint i = 0; i < 16; i++)
			coefs[i] = (s16)tr_be16 (h + 2 * i);
		int h1 = 0, h2 = 0;
		const u8 *src = raw + c * chsize;
		for (u32 done = 0, f = 0; done < a->samples; f++)
		{
			s16 out[14];
			const uint cnt = a->samples - done < 14 ? a->samples - done : 14;
			DspAdpcmDecodeBlock (src + (size_t)f * 8, cnt, out, coefs, &h1, &h2);
			for (uint k = 0; k < cnt; k++)
			{
				u8 *o = w + 44 + 2 * ((size_t)(done + k) * ch + c);
				o[0] = out[k], o[1] = out[k] >> 8;
			}
			done += cnt;
		}
	}
	*wav = w;
	*wav_size = bytes;
	return ERR_OK;
}
