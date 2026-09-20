// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Humongous / Cat Daddy Resource.rez; see lib-rez.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-rez.h"
#include "lib-excite.h"
#include <string.h>

#define REZ_TEXTURE 73
#define REZ_MAX_UNPACKED (64u << 20)

static u32 rz_be32 (const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static u32 rz_be16 (const u8 *p) { return p[0] << 8 | p[1]; }

// Locate the slot table: false if the footer does not describe one.
static bool rz_slots (const u8 *d, size_t size, const u8 **slots, uint *n)
{
	if (size < 4096)
		return false;
	const size_t fs = size - 2048;
	const size_t count = rz_be32 (d + fs + 8);
	const size_t tb = (count * 24 + 2047) / 2048 * 2048;
	if (!count || count > 0x100000 || tb > fs)
		return false;
	*slots = d + fs - tb;
	*n = count;
	return true;
}

bool IsHumongousRez (const u8 *d, size_t size)
{
	const u8 *slots;
	uint n;
	if (!rz_slots (d, size, &slots, &n))
		return false;
	uint groups = 0;
	for (uint i = 0; i < n && groups < 4; i++)
	{
		const u8 *s = slots + (size_t)i * 24;
		const size_t off = rz_be32 (s), sz = rz_be32 (s + 4), hs = rz_be16 (s + 14);
		if (sz && hs >= 0x20 && hs <= sz && off + sz <= size - 2048 && rz_be32 (d + off + sz - hs) < 0x10000)
			groups++;
	}
	return groups > 0;
}

// Returns the unpacked size, or 0 on error.
static size_t rz_unpack (const u8 *d, size_t size, size_t p, u8 *out, size_t cap)
{
	if (p + 4 > size)
		return 0;
	const size_t n = rz_be32 (d + p);
	if (n > cap)
		return 0;
	p += 4;
	size_t o = 0;
	while (o < n)
	{
		if (p >= size)
			return 0;
		const uint b = d[p++], c = b & 0x7f;
		if (b & 0x80)
		{
			if (p + c > size || o + c > n)
				return 0;
			memcpy (out + o, d + p, c);
			p += c;
			o += c;
		}
		else
		{
			if (p + 2 > size)
				return 0;
			const size_t dist = rz_be16 (d + p);
			p += 2;
			if (dist > o || o + c > n)
				return 0;
			for (uint i = 0; i < c; i++, o++)
				out[o] = out[o - dist];
		}
	}
	return n;
}

rez_texture_t *ListRezTextures (const u8 *d, size_t size, uint *count)
{
	*count = 0;
	const u8 *slots;
	uint n;
	if (!rz_slots (d, size, &slots, &n))
		return 0;
	uint cap = 256;
	rez_texture_t *list = MALLOC (cap * sizeof (*list));
	if (!list)
		return 0;
	for (uint g = 0; g < n; g++)
	{
		const u8 *s = slots + (size_t)g * 24;
		const size_t off = rz_be32 (s), sz = rz_be32 (s + 4), hs = rz_be16 (s + 14);
		if (!sz || hs < 0x20 || hs > sz || off + sz > size - 2048)
			continue;
		const u8 *t = d + off + sz - hs;
		const size_t cnt = rz_be32 (t);
		if (!cnt || 16 + cnt * 24 > hs || rz_be32 (t + 16) < off || rz_be32 (t + 16) >= off + sz)
			continue;
		for (uint i = 0; i < cnt; i++)
		{
			const u8 *r = t + 16 + i * 24;
			if ((s16)rz_be16 (r + 12) != REZ_TEXTURE)
				continue;
			rez_texture_t e;
			e.group = g;
			e.index = i;
			e.offset = rz_be32 (r);
			e.csize = rz_be32 (r + 4);
			e.usize = rz_be32 (r + 8);
			e.flags = rz_be16 (r + 14);
			if (e.offset + (size_t)e.csize > size || !e.usize || e.usize > REZ_MAX_UNPACKED)
				continue;
			if (*count == cap)
			{
				cap *= 2;
				rez_texture_t *nl = REALLOC (list, cap * sizeof (*list));
				if (!nl)
					return list;
				list = nl;
			}
			list[(*count)++] = e;
		}
	}
	return list;
}

enumError DecodeRezTexture (u8 **rgba, uint *width, uint *height, const u8 *d, size_t size,
	const rez_texture_t *t)
{
	u8 *buf = MALLOC ((size_t)t->usize + 64);
	if (!buf)
		return ERR_OUT_OF_MEMORY;
	size_t n;
	if (t->flags & 1)
		n = rz_unpack (d, size, t->offset, buf, t->usize + 64);
	else
		memcpy (buf, d + t->offset, n = t->csize < t->usize ? t->csize : t->usize);
	if (n < 0x60)
	{
		FREE (buf);
		return ERR_INVALID_DATA;
	}
	const uint w = rz_be16 (buf), h = rz_be16 (buf + 2), fmt = rz_be16 (buf + 6);
	size_t body;
	const u8 *pal = 0;
	uint pal_count = 0;
	enumError err = ERR_INVALID_DATA;
	if (!w || !h || w > 2048 || h > 2048)
		goto done;
	if (fmt == 9 || fmt == 8)
	{
		pal_count = fmt == 9 ? 256 : 16;
		body = fmt == 9 ? (size_t)w * h : (size_t)w * h / 2;
		if (n < 0x80 + body + 2 * pal_count)
			goto done;
		pal = buf + 0x80 + body;
		err = DecodeGXTexture_RGBA (rgba, w, h, fmt, buf + 0x80, (uint)body, pal, pal_count, 2);
	}
	else if (fmt == 5 || fmt == 6)
	{
		body = (size_t)w * h * (fmt == 6 ? 4 : 2);
		if (n < 0x60 + body)
			goto done;
		err = DecodeGXTexture_RGBA (rgba, w, h, fmt, buf + 0x60, (uint)body, 0, 0, 0);
	}
done:
	FREE (buf);
	*width = w;
	*height = h;
	return err;
}
