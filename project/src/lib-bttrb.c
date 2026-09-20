// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Blue Tongue "TRB\0" package reader; see lib-bttrb.h.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-bttrb.h"
#include "lib-excite.h"

#define BT_MAX_SECTIONS 1024
#define BT_MAX_SYMBOLS 200000
#define BT_MAX_TEXTURES 20000
#define BT_FILL 0x0df00bb0u

static u32 bt_u32 (const u8 *d, size_t size, size_t o)
{
	if (o + 4 > size)
		return 0;
	const u8 *p = d + o;
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static uint bt_u16 (const u8 *d, size_t size, size_t o)
{
	return o + 2 > size ? 0 : d[o] << 8 | d[o + 1];
}

bool IsBlueTongueTrb (const u8 *d, size_t size)
{
	if (size < 0x100 || size > 0xffffffffu || memcmp (d, "TRB\0", 4) || bt_u32 (d, size, 4) != 0x7d1)
		return false;
	const u32 n = bt_u32 (d, size, 0x0c);
	return n && n <= BT_MAX_SECTIONS && 0x80 + 0x30ull * n <= size;
}

typedef struct bt_sec_t
{
	u32 name, size, off;
} bt_sec_t;

static bt_sec_t bt_section (const u8 *d, size_t size, uint i)
{
	const size_t o = 0x80 + 0x30 * (size_t)i;
	bt_sec_t s = { bt_u32 (d, size, o + 4), bt_u32 (d, size, o + 0x10), bt_u32 (d, size, o + 0x18) };
	if ((u64)s.off + s.size > size)
		s.size = 0;
	return s;
}

// NUL-terminated string of the string table (section 0), or "".
static ccp bt_name (const u8 *d, size_t size, u32 name_off)
{
	const bt_sec_t t = bt_section (d, size, 0);
	const u64 o = (u64)t.off + name_off;
	if (!t.size || name_off >= t.size)
		return "";
	return memchr (d + o, 0, size - o) ? (ccp)d + o : "";
}

const u8 *FindBlueTongueSection (const u8 *d, size_t size, ccp name, u32 *len)
{
	if (!IsBlueTongueTrb (d, size))
		return 0;
	const uint n = bt_u32 (d, size, 0x0c);
	for (uint i = 0; i < n; i++)
	{
		const bt_sec_t s = bt_section (d, size, i);
		if (s.size && !strcmp (bt_name (d, size, s.name), name))
		{
			*len = s.size;
			return d + s.off;
		}
	}
	return 0;
}

static bool bt_dim (uint v)
{
	return v && v <= 4096;
}

static void bt_clean_name (char *out, size_t out_size, ccp src, uint fallback)
{
	char tmp[256];
	snprintf (tmp, sizeof (tmp), "%s", src);
	ccp base = tmp;
	for (ccp p = tmp; *p; p++)
		if (*p == '\\' || *p == '/')
			base = p + 1;
	char tmp2[256];
	snprintf (tmp2, sizeof (tmp2), "%s", base);
	char *dot = strrchr (tmp2, '.');
	if (dot && dot != tmp2 && strlen (dot) <= 5)
		*dot = 0;
	size_t o = 0;
	for (ccp p = tmp2; *p && o + 1 < out_size; p++)
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

static void bt_unique (bttrb_tex_t *list, uint n)
{
	char base[sizeof (list->name)];
	snprintf (base, sizeof (base), "%s", list[n].name);
	for (uint k = 2;; k++)
	{
		bool clash = false;
		for (uint i = 0; i < n && !clash; i++)
			clash = !strcasecmp (list[i].name, list[n].name);
		if (!clash)
			return;
		snprintf (list[n].name, sizeof (list[n].name), "%.80s_%u", base, k);
	}
}

bttrb_tex_t *ListBlueTongueTextures (const u8 *d, size_t size, uint *count)
{
	*count = 0;
	if (!IsBlueTongueTrb (d, size))
		return 0;
	const uint nsec = bt_u32 (d, size, 0x0c), nsym = bt_u32 (d, size, 0x14);
	const size_t symtab = 0x80 + 0x30 * (size_t)nsec;
	if (!nsym || nsym > BT_MAX_SYMBOLS || symtab + 16ull * nsym > size)
		return 0;

	bt_sec_t pool = { 0, 0, 0 };
	for (uint i = 0; i < nsec; i++)
	{
		const bt_sec_t s = bt_section (d, size, i);
		if (s.size && !strcmp (bt_name (d, size, s.name), "__s00000"))
			pool = s;
	}
	if (!pool.size)
		return 0;

	bttrb_tex_t *list = CALLOC (BT_MAX_TEXTURES, sizeof (*list));
	uint n = 0;
	for (uint i = 0; i < nsym && n < BT_MAX_TEXTURES; i++)
	{
		const size_t so = symtab + 16 * (size_t)i;
		if (strcmp (bt_name (d, size, bt_u32 (d, size, so + 12)), "ttex"))
			continue;
		const uint si = bt_u32 (d, size, so + 8) >> 16;
		if (si >= nsec)
			continue;
		const bt_sec_t s = bt_section (d, size, si);
		const u64 obj = (u64)s.off + bt_u32 (d, size, so + 4);
		if (!s.size || obj + 0x60 > size)
			continue;

		size_t f = obj + 0xc;
		for (uint k = 0; k < 16 && bt_u32 (d, size, f) == BT_FILL; k++)
			f += 4;
		if (f + 0x60 > size)
			continue;

		bttrb_tex_t *t = list + n;
		t->format = bt_u32 (d, size, f);
		const u32 pix = bt_u32 (d, size, f + 0xc), bytes = bt_u32 (d, size, f + 0x1c);
		uint hw = 0x40;
		if (!bt_dim (bt_u16 (d, size, f + hw)) || !bt_dim (bt_u16 (d, size, f + hw + 2)))
			hw = 0x44;
		t->width = bt_u16 (d, size, f + hw);
		t->height = bt_u16 (d, size, f + hw + 2);
		if (!bt_dim (t->width) || !bt_dim (t->height) || t->format > 14 || !bytes
			|| (u64)pix + bytes > pool.size)
			continue;
		t->off = pool.off + pix;
		t->size = bytes;
		const u32 pal = bt_u32 (d, size, f + 0x14);
		t->pal_off = pal ? pool.off + pal : 0;
		t->pal_format = bt_u32 (d, size, f + 0x18);

		size_t nm = f + hw + 8;
		while (nm < f + hw + 0x30 && d[nm] < 0x20)
			nm++;
		char raw[128] = "";
		for (uint k = 0; k + 1 < sizeof (raw) && nm + k < size && d[nm + k]; k++)
			raw[k] = d[nm + k], raw[k + 1] = 0;
		bt_clean_name (t->name, sizeof (t->name), raw, n);
		bt_unique (list, n);
		n++;
	}
	if (!n)
	{
		FREE (list);
		return 0;
	}
	*count = n;
	return list;
}

enumError DecodeBlueTongueTexture (u8 **rgba, const u8 *d, size_t size, const bttrb_tex_t *t)
{
	if ((u64)t->off + t->size > size)
		return ERR_NOTHING_TO_DO;
	const u8 *pal = 0;
	uint pal_count = 0;
	if (t->format == 8 || t->format == 9)
	{
		pal_count = t->format == 8 ? 16 : 256;
		if (!t->pal_off || (u64)t->pal_off + 2 * pal_count > size)
			return ERR_NOTHING_TO_DO;
		pal = d + t->pal_off;
	}
	return DecodeGXTexture_RGBA (rgba, t->width, t->height, t->format, d + t->off, t->size, pal, pal_count,
		t->pal_format);
}
