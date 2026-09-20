// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Atomic Planet PUB packages; see lib-pub.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-pub.h"
#include "lib-excite.h"
#include <string.h>

static u32 pb_be32 (const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static u32 pb_be16 (const u8 *p) { return p[0] << 8 | p[1]; }

bool IsAtomicPub (const u8 *d, size_t size)
{
	if (size < 0x60 || pb_be32 (d) != 1 || pb_be32 (d + 4) != 1 || pb_be32 (d + 8) != size - 0x20
		|| pb_be32 (d + 0x20) != 1)
		return false;
	const size_t n = pb_be32 (d + 0x24), tbl = pb_be32 (d + 0x28);
	return n && n < 0x100000 && tbl >= 0x40 && tbl + n * 28 <= size;
}

pub_texture_t *ListPubTextures (const u8 *d, size_t size, uint *count)
{
	*count = 0;
	if (!IsAtomicPub (d, size))
		return 0;
	const uint n = pb_be32 (d + 0x24);
	const size_t tbl = pb_be32 (d + 0x28);
	pub_texture_t *list = MALLOC (n * sizeof (*list));
	if (!list)
		return 0;
	for (uint i = 0; i < n; i++)
	{
		const u8 *e = d + tbl + (size_t)i * 28;
		const size_t off = pb_be32 (e + 4), sz = pb_be32 (e + 12);
		if (pb_be16 (e + 8) != 1 || off < 0x40 || sz < 0x30 || off + sz > size)
			continue;
		const u8 *o = d + off;
		pub_texture_t t;
		memset (&t, 0, sizeof (t));
		t.hash = pb_be32 (e);
		t.gx_format = o[0x21];
		t.pal_format = o[0x22];
		t.pal_count = pb_be16 (o + 0x1c);
		t.height = pb_be16 (o + 0x2c);
		t.width = pb_be16 (o + 0x2e);
		const size_t img = pb_be32 (o + 0x24), isz = pb_be32 (o + 0x28), pal = pb_be32 (o + 0x18);
		if (!o[0x20] || !t.width || !t.height || t.width > 4096 || t.height > 4096 || !isz
			|| img + isz > sz)
			continue;
		if (t.gx_format == 9)
		{
			if (!t.pal_count || pal + 2 * t.pal_count > sz)
				continue;
			t.palette = o + pal;
		}
		else if (t.gx_format != 6 && t.gx_format != 1)
			continue;
		t.pixels = o + img;
		t.pixel_size = isz;
		list[(*count)++] = t;
	}
	return list;
}

enumError DecodePubTexture (u8 **rgba, const pub_texture_t *t)
{
	return DecodeGXTexture_RGBA (rgba, t->width, t->height, t->gx_format, t->pixels,
		t->pixel_size > 0xffffffffu ? 0xffffffffu : (uint)t->pixel_size, t->palette, t->pal_count,
		t->pal_format);
}
