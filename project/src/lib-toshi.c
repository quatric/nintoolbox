// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Toshi TSFB containers and texture libraries; see lib-toshi.h.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-toshi.h"
#include "lib-excite.h"
#include <string.h>

#define TOSHI_MAX_SECTION (512u << 20)

static u32 ts_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
static u32 ts_be16 (const u8 *p)
{
	return p[0] << 8 | p[1];
}

bool IsToshiTsfb (const u8 *d, size_t size)
{
	return size >= 32 && !memcmp (d, "TSFB", 4) && !memcmp (d + 8, "FBRT", 4);
}

static enumError ts_btec (u8 **out, size_t *out_size, const u8 *b, size_t n)
{
	if (n < 16 || memcmp (b, "CETB", 4))
		return ERR_INVALID_DATA;
	const uint major = ts_be16 (b + 4), minor = ts_be16 (b + 6);
	const u32 csize = ts_be32 (b + 8), size = ts_be32 (b + 12);
	size_t p = 16;
	u32 xor = 0;
	if (major != 1 || (minor != 2 && minor != 3) || size > TOSHI_MAX_SECTION)
		return ERR_INVALID_DATA;
	if (minor == 3)
	{
		if (n < 20)
			return ERR_INVALID_DATA;
		xor= ts_be32 (b + 16);
		p = 20;
	}
	u8 *o = MALLOC (size ? size : 1);
	if (!o)
		return ERR_OUT_OF_MEMORY;
	size_t op = 0;
	s64 left = csize;
	while (left > 0)
	{
		if (p >= n)
			goto bad;
		const uint c = b[p++];
		uint used = 1, sz = c & 0x3f;
		if (c & 0x40)
		{
			if (p >= n)
				goto bad;
			sz = sz << 8 | b[p++];
			used++;
		}
		uint off = 0;
		if (!(c & 0x80))
		{
			if (p >= n)
				goto bad;
			off = b[p++];
			used++;
			if (off & 0x80)
			{
				if (p >= n)
					goto bad;
				off = (off & 0x7f) << 8 | b[p++];
				used++;
			}
		}
		sz++;
		left -= used;
		if (c & 0x80)
		{
			if (p + sz > n || op + sz > size)
				goto bad;
			memcpy (o + op, b + p, sz);
			p += sz;
			op += sz;
			left -= sz;
		}
		else
		{
			off++;
			if (off > op || op + sz > size)
				goto bad;
			for (uint i = 0; i < sz; i++, op++)
				o[op] = o[op - off];
		}
	}
	if (op != size)
		goto bad;
	if (xor)
		for (size_t i = 0; i < size; i++)
			o[i] ^= (u8) xor ;
	*out = o;
	*out_size = size;
	return ERR_OK;
bad:
	FREE (o);
	return ERR_INVALID_DATA;
}

void ResetToshiTrb (toshi_trb_t *t)
{
	FREE (t->sect);
	memset (t, 0, sizeof (*t));
}

enumError OpenToshiTrb (toshi_trb_t *t, const u8 *d, size_t size)
{
	memset (t, 0, sizeof (*t));
	if (!IsToshiTsfb (d, size))
		return ERR_INVALID_DATA;
	size_t p = 12;
	enumError err = ERR_OK;
	while (!err && p + 8 <= size)
	{
		const u32 s = ts_be32 (d + p + 4);
		const u8 *pl = d + p + 8;
		if (s > size - p - 8)
			break;
		if (!memcmp (d + p, "TCES", 4) && !t->sect)
		{
			t->sect = MALLOC (s ? s : 1);
			if (!t->sect)
				return ERR_OUT_OF_MEMORY;
			memcpy (t->sect, pl, s);
			t->sect_size = s;
		}
		else if (!memcmp (d + p, "CCES", 4) && !t->sect)
			err = ts_btec (&t->sect, &t->sect_size, pl, s);
		else if (!memcmp (d + p, "BMYS", 4))
			t->symb = pl, t->symb_size = s;
		p += 8 + ((size_t)s + 3 & ~(size_t)3);
	}
	if (!err && (!t->sect || !t->symb || t->symb_size < 4))
		err = ERR_INVALID_DATA;
	if (err)
		ResetToshiTrb (t);
	return err;
}

s64 ToshiSymbol (const toshi_trb_t *t, ccp name)
{
	const u32 n = ts_be32 (t->symb);
	if (n > (t->symb_size - 4) / 12)
		return -1;
	const size_t names = 4 + 12 * (size_t)n;
	for (u32 i = 0; i < n; i++)
	{
		const u8 *e = t->symb + 4 + 12 * i;
		const size_t no = names + ts_be16 (e + 2);
		if (no < t->symb_size && !strncmp ((ccp)t->symb + no, name, t->symb_size - no))
		{
			const u32 off = ts_be32 (e + 8);
			return off < t->sect_size ? (s64)off : -1;
		}
	}
	return -1;
}

// 64-bit on purpose: callers pass count * stride products that must not be
// truncated before the check.
static bool ts_ptr (const toshi_trb_t *t, u64 off, u64 len)
{
	return off <= t->sect_size && len <= t->sect_size - off;
}

static uint ts_ttl_entries (const toshi_trb_t *t, u32 *ents)
{
	const s64 base = ToshiSymbol (t, "TTL");
	if (base < 0 || !ts_ptr (t, base, 12))
		return 0;
	const u32 n = ts_be32 (t->sect + base), po = ts_be32 (t->sect + base + 4);
	if (!n || n > 65536 || !ts_ptr (t, po, n * 52))
		return 0;
	*ents = po;
	return n;
}

uint ToshiTtlCount (const toshi_trb_t *t)
{
	u32 po;
	return ts_ttl_entries (t, &po);
}

enumError DecodeToshiTexture (
	const toshi_trb_t *t, uint idx, ccp *name, u8 **rgba, uint *width, uint *height)
{
	u32 po;
	const uint n = ts_ttl_entries (t, &po);
	if (idx >= n)
		return ERR_INVALID_DATA;
	const u8 *e = t->sect + po + 52 * idx;
	const u32 fmt = ts_be32 (e), np = ts_be32 (e + 4), w = ts_be32 (e + 8), h = ts_be32 (e + 12);
	const u32 dp = ts_be32 (e + 20), dsz = ts_be32 (e + 24), pp = ts_be32 (e + 28);
	const u32 npal = ts_be32 (e + 36);
	(void)e;
	static const u8 gx[] = { 0, 2, 1, 3, 4, 5, 6, 14 }; // 0x301..0x308
	static const u8 ci_gx[] = { 8, 8, 9, 9, 9 }, ci_pal[] = { 1, 2, 1, 2, 0 }; // 0x30c..0x310
	uint gxfmt;
	const u8 *pal = 0;
	uint pal_fmt = 0;
	if (fmt >= 0x301 && fmt <= 0x308)
		gxfmt = gx[fmt - 0x301];
	else if (fmt >= 0x30c && fmt <= 0x310)
	{
		gxfmt = ci_gx[fmt - 0x30c];
		pal_fmt = ci_pal[fmt - 0x30c];
		if (!npal || npal > 256 || !ts_ptr (t, pp, npal * 2))
			return ERR_INVALID_DATA;
		pal = t->sect + pp;
	}
	else
		return ERR_INVALID_DATA;
	if (!w || !h || w > 4096 || h > 4096 || !ts_ptr (t, dp, dsz) || !ts_ptr (t, np, 1))
		return ERR_INVALID_DATA;
	*name = (ccp)t->sect + np;
	const enumError err
		= DecodeGXTexture_RGBA (rgba, w, h, gxfmt, t->sect + dp, dsz, pal, npal, pal_fmt);
	if (!err)
		*width = w, *height = h;
	return err;
}

static s32 ts_be32s (const u8 *p)
{
	return (s32)ts_be32 (p);
}

static float ts_bef32 (const u8 *p)
{
	const u32 u = ts_be32 (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

enumError OpenToshiTkl (const toshi_trb_t *t, toshi_tkl_t *tkl)
{
	memset (tkl, 0, sizeof (*tkl));
	const s64 base = ToshiSymbol (t, "keylib");
	if (base < 0 || !ts_ptr (t, base, 52))
		return ERR_INVALID_DATA;
	const u8 *h = t->sect + base;
	const u32 no = ts_be32 (h);
	if (!ts_ptr (t, no, 1))
		return ERR_INVALID_DATA;
	tkl->name = (ccp)t->sect + no;
	tkl->scale[0] = ts_bef32 (h + 4);
	tkl->scale[1] = ts_bef32 (h + 8);
	tkl->scale[2] = ts_bef32 (h + 12);
	tkl->num_t = (u32)ts_be32s (h + 16);
	tkl->num_q = (u32)ts_be32s (h + 20);
	tkl->num_s = (u32)ts_be32s (h + 24);
	tkl->tsize = (u32)ts_be32s (h + 28);
	tkl->qsize = (u32)ts_be32s (h + 32);
	tkl->ssize = (u32)ts_be32s (h + 36);
	const u32 to = ts_be32 (h + 40), qo = ts_be32 (h + 44), so = ts_be32 (h + 48);
	if (!ts_ptr (t, to, (u64)tkl->num_t * tkl->tsize)
		|| !ts_ptr (t, qo, (u64)tkl->num_q * tkl->qsize)
		|| !ts_ptr (t, so, (u64)tkl->num_s * tkl->ssize))
		return ERR_INVALID_DATA;
	tkl->t_data = t->sect + to;
	tkl->q_data = t->sect + qo;
	tkl->s_data = t->sect + so;
	return ERR_OK;
}

bool ToshiTklTranslation (const toshi_tkl_t *t, uint idx, float out[3])
{
	if (idx >= t->num_t || t->tsize < 6)
		return false;
	const u8 *p = t->t_data + (size_t)idx * t->tsize;
	for (uint i = 0; i < 3; i++)
		out[i] = (s16)ts_be16 (p + 2 * i) * t->scale[i];
	return true;
}

bool ToshiTklQuaternion (const toshi_tkl_t *t, uint idx, float out[4])
{
	if (idx >= t->num_q || t->qsize < 8)
		return false;
	const u8 *p = t->q_data + (size_t)idx * t->qsize;
	for (uint i = 0; i < 4; i++)
		out[i] = (s16)ts_be16 (p + 2 * i) / 32767.0f;
	return true;
}

bool ToshiTklScale (const toshi_tkl_t *t, uint idx, float *out)
{
	if (idx >= t->num_s)
		return false;
	const u8 *p = t->s_data + (size_t)idx * t->ssize;
	// BEST-EFFORT: no disc sample with numScales>0 has been found to confirm
	// this branch; float is the natural size match, s16/32767 is by analogy
	// with the verified quaternion encoding.
	*out = t->ssize >= 4 ? ts_bef32 (p) : t->ssize == 2 ? (s16)ts_be16 (p) / 32767.0f : 0.0f;
	return true;
}
