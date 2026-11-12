// SPDX-License-Identifier: GPL-2.0+
// Havok TAG0 tag files to YAML through their reflection data; see lib-hktag.h.

#include "lib-hktag.h"
#include "lib-sbuf.h"

typedef struct hk_member_t
{
	u32 name, flags, offset, type;
} hk_member_t;

typedef struct hk_type_t
{
	u32 name; // TST1 index
	u32 parent, flags, format, sub, size, align;
	bool has_format, has_sub, has_size;
	hk_member_t *m;
	u32 n_m;
} hk_type_t;

typedef struct hk_item_t
{
	u32 type, flags, off, count;
} hk_item_t;

typedef struct hk_t
{
	const u8 *data;
	size_t data_n;
	ccp *tst, *fst;
	u32 n_tst, n_fst;
	hk_type_t *types;
	u32 n_types;
	hk_item_t *items;
	u32 n_items;
	bool bad;
	sbuf_t b;
} hk_t;

static u32 hk_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static u32 hk_le32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static bool hk_varint (const u8 *b, size_t n, size_t *i, u32 *out)
{
	if (*i >= n)
		return false;
	const u8 x = b[*i];
	uint len;
	u64 v;
	if (x < 0x80)
		len = 1, v = x;
	else if (x < 0xc0)
		len = 2, v = x & 0x3f;
	else if (x < 0xe0)
		len = 3, v = x & 0x1f;
	else if (x < 0xe8)
		len = 4, v = x & 0x07;
	else if (x < 0xf0)
		len = 5, v = x & 0x07;
	else
		return false;
	if (*i + len > n)
		return false;
	for (uint k = 1; k < len; k++)
		v = v << 8 | b[*i + k];
	*i += len;
	*out = (u32)v;
	return true;
}

// Splits a NUL separated string section into an array of pointers.
static ccp *hk_strings (const u8 *p, size_t n, u32 *count)
{
	u32 c = 0;
	for (size_t i = 0; i < n; i++)
		c += !p[i];
	ccp *v = CALLOC (c + 1, sizeof (*v));
	u32 k = 0;
	for (size_t i = 0; i < n && k < c;)
	{
		v[k++] = (ccp)p + i;
		while (i < n && p[i])
			i++;
		i++;
	}
	*count = k;
	return v;
}

// Sub-section of [P, END) with TAG; returns data pointer and size.
static const u8 *hk_find (const u8 *p, const u8 *end, ccp tag, size_t *size)
{
	while (p + 8 <= end)
	{
		const u32 sz = hk_be32 (p) & 0x3fffffff;
		if (sz < 8 || sz > (size_t)(end - p))
			return 0;
		if (!memcmp (p + 4, tag, 4))
		{
			*size = sz - 8;
			return p + 8;
		}
		p += sz;
	}
	return 0;
}

static void hk_free (hk_t *h)
{
	for (u32 i = 0; h->types && i < h->n_types; i++)
		FREE (h->types[i].m);
	FREE (h->types);
	FREE (h->items);
	FREE (h->tst);
	FREE (h->fst);
}

static bool hk_parse (hk_t *h, const u8 *d, size_t n)
{
	memset (h, 0, sizeof (*h));
	if (!IsHavokTagFile (d, n))
		return false;
	const u8 *end = d + (hk_be32 (d) & 0x3fffffff);
	size_t type_n, indx_n, tst_n, fst_n, tna_n, tbd_n, item_n;
	h->data = hk_find (d + 8, end, "DATA", &h->data_n);
	const u8 *type = hk_find (d + 8, end, "TYPE", &type_n),
			 *indx = hk_find (d + 8, end, "INDX", &indx_n);
	if (!h->data || !type || !indx)
		return false;
	const u8 *tend = type + type_n;
	const u8 *tst = hk_find (type, tend, "TST1", &tst_n),
			 *fst = hk_find (type, tend, "FST1", &fst_n),
			 *tna = hk_find (type, tend, "TNA1", &tna_n),
			 *tbd = hk_find (type, tend, "TBDY", &tbd_n),
			 *item = hk_find (indx, indx + indx_n, "ITEM", &item_n);
	if (!tst || !fst || !tna || !tbd || !item)
		return false;
	h->tst = hk_strings (tst, tst_n, &h->n_tst);
	h->fst = hk_strings (fst, fst_n, &h->n_fst);

	size_t i = 0;
	u32 nt;
	if (!hk_varint (tna, tna_n, &i, &nt) || nt > 0x100000)
		return false;
	h->n_types = nt;
	h->types = CALLOC (nt + 1, sizeof (*h->types));
	for (u32 t = 1; t < nt; t++)
	{
		u32 tc, x;
		if (!hk_varint (tna, tna_n, &i, &h->types[t].name) || !hk_varint (tna, tna_n, &i, &tc))
			return false;
		for (u32 k = 0; k < tc; k++)
			if (!hk_varint (tna, tna_n, &i, &x) || !hk_varint (tna, tna_n, &i, &x))
				return false;
	}
	for (i = 0; i < tbd_n;)
	{
		u32 t, v;
		if (!hk_varint (tbd, tbd_n, &i, &t))
			return false;
		if (!t)
			continue;
		if (t >= nt)
			return false;
		hk_type_t *y = h->types + t;
		if (!hk_varint (tbd, tbd_n, &i, &y->parent) || !hk_varint (tbd, tbd_n, &i, &y->flags)
			|| y->parent >= nt)
			return false;
		const u32 f = y->flags;
		if (f & ~0x7fu)
			return false;
		if (f & 1 && !(y->has_format = hk_varint (tbd, tbd_n, &i, &y->format)))
			return false;
		if (f & 2 && !(y->has_sub = hk_varint (tbd, tbd_n, &i, &y->sub)))
			return false;
		if (f & 4 && !hk_varint (tbd, tbd_n, &i, &v))
			return false;
		if (f & 8
			&& !((y->has_size = hk_varint (tbd, tbd_n, &i, &y->size))
				&& hk_varint (tbd, tbd_n, &i, &y->align)))
			return false;
		if (f & 0x10 && !hk_varint (tbd, tbd_n, &i, &v))
			return false;
		if (f & 0x20)
		{
			if (!hk_varint (tbd, tbd_n, &i, &y->n_m) || y->n_m > 0x10000)
				return false;
			FREE (y->m);
			y->m = CALLOC (y->n_m + 1, sizeof (*y->m));
			for (u32 k = 0; k < y->n_m; k++)
			{
				hk_member_t *m = y->m + k;
				if (!hk_varint (tbd, tbd_n, &i, &m->name) || !hk_varint (tbd, tbd_n, &i, &m->flags)
					|| (m->flags & 0x80
						&& !hk_varint (
							tbd, tbd_n, &i, &v)) // extra value, e.g. hkPackedVector3::values
					|| !hk_varint (tbd, tbd_n, &i, &m->offset)
					|| !hk_varint (tbd, tbd_n, &i, &m->type) || m->type >= nt)
					return false;
			}
		}
		if (f & 0x40)
		{
			u32 ic, a, c;
			if (!hk_varint (tbd, tbd_n, &i, &ic))
				return false;
			for (u32 k = 0; k < ic; k++)
				if (!hk_varint (tbd, tbd_n, &i, &a) || !hk_varint (tbd, tbd_n, &i, &c))
					return false;
		}
	}
	h->n_items = (u32)(item_n / 12);
	h->items = CALLOC (h->n_items + 1, sizeof (*h->items));
	for (u32 k = 0; k < h->n_items; k++)
	{
		const u8 *p = item + 12 * k;
		const u32 tf = hk_le32 (p);
		h->items[k].type = tf & 0xffffff;
		h->items[k].flags = tf >> 24;
		h->items[k].off = hk_le32 (p + 4);
		h->items[k].count = hk_le32 (p + 8);
		if (h->items[k].type >= nt)
			return false;
	}
	return true;
}

// Format, size and subtype of type T, inherited through the parents.
static void hk_info (const hk_t *h, u32 t, u32 *format, u32 *size, u32 *sub)
{
	bool hf = false, hs = false, hb = false;
	*format = *size = *sub = 0;
	for (uint guard = 0; t && t < h->n_types && guard < 64; guard++)
	{
		const hk_type_t *y = h->types + t;
		if (!hf && y->has_format)
			hf = true, *format = y->format;
		if (!hs && y->has_size)
			hs = true, *size = y->size;
		if (!hb && y->has_sub)
			hb = true, *sub = y->sub;
		if (hf && hs && hb)
			break;
		t = y->parent;
	}
}

static ccp hk_type_name (const hk_t *h, u32 t)
{
	return t && t < h->n_types && h->types[t].name < h->n_tst ? h->tst[h->types[t].name] : "?";
}

static const u8 *hk_at (hk_t *h, u64 off, u64 len)
{
	if (off > h->data_n || len > h->data_n - off)
	{
		h->bad = true;
		return 0;
	}
	return h->data + off;
}

static u64 hk_u64 (hk_t *h, u64 off)
{
	const u8 *p = hk_at (h, off, 8);
	return p ? hk_le32 (p) | (u64)hk_le32 (p + 4) << 32 : 0;
}

static void hk_ind (hk_t *h, uint n)
{
	static const char sp[] = "                                                                ";
	sb_write (&h->b, sp, n < 64 ? n : 64);
}

static bool hk_is_scalar (const hk_t *h, u32 t, uint depth)
{
	u32 f, s, sub;
	hk_info (h, t, &f, &s, &sub);
	const uint k = f & 0x1f;
	if (k == 8 && f & 0x20)
		return depth < 8 && hk_is_scalar (h, sub, depth + 1);
	return k >= 2 && k <= 6;
}

// Inline (flow) value of a scalar or fixed array of scalars.
static void hk_flow (hk_t *h, u32 t, u64 off, uint depth)
{
	u32 f, size, sub;
	hk_info (h, t, &f, &size, &sub);
	const u8 *p;
	switch (f & 0x1f)
	{
		case 2:
			if ((p = hk_at (h, off, 1)))
				sb_puts (&h->b, *p ? "true" : "false");
			return;
		case 4:
		{
			if (!size || size > 8 || !(p = hk_at (h, off, size)))
			{
				sb_puts (&h->b, "~");
				return;
			}
			u64 v = 0;
			for (int i = (int)size - 1; i >= 0; i--)
				v = v << 8 | p[i];
			if (f & 0x200 && size < 8 && v >> (size * 8 - 1) & 1)
				v |= ~0ull << (size * 8);
			if (f & 0x200)
				sb_printf (&h->b, "%lld", (long long)v);
			else
				sb_printf (&h->b, "%llu", (unsigned long long)v);
			return;
		}
		case 5:
			if (size == 4 && (p = hk_at (h, off, 4)))
			{
				const u32 v = hk_le32 (p);
				float x;
				memcpy (&x, &v, 4);
				if (x != x)
					sb_puts (&h->b, ".nan");
				else
					sb_printf (&h->b, "%.9g", x);
			}
			else if (size == 8)
			{
				const u64 v = hk_u64 (h, off);
				double x;
				memcpy (&x, &v, 8);
				sb_printf (&h->b, "%.17g", x);
			}
			else if (size == 2 && (p = hk_at (h, off, 2)))
				sb_printf (&h->b, "0x%04x", p[0] | p[1] << 8);
			else
				sb_puts (&h->b, "~");
			return;
		case 3:
		{
			const u64 idx = hk_u64 (h, off);
			if (!idx)
				sb_puts (&h->b, "~");
			else if (idx >= h->n_items)
				h->bad = true;
			else if ((p = hk_at (h, h->items[idx].off, h->items[idx].count)))
				sb_quote (&h->b, (ccp)p, strnlen ((ccp)p, h->items[idx].count));
			return;
		}
		case 6:
		{
			const u64 idx = hk_u64 (h, off);
			if (idx >= h->n_items)
				h->bad = true;
			else if (idx)
				sb_printf (&h->b, "\"@%llu\"", (unsigned long long)idx);
			else
				sb_puts (&h->b, "~");
			return;
		}
		case 8: // fixed array of scalars
		{
			u32 ef, es, esub;
			hk_info (h, sub, &ef, &es, &esub);
			sb_puts (&h->b, "[");
			for (u32 i = 0; i < f >> 8 && !h->bad && depth < 8; i++)
			{
				if (i)
					sb_puts (&h->b, ", ");
				hk_flow (h, sub, off + (u64)es * i, depth + 1);
			}
			sb_puts (&h->b, "]");
			return;
		}
		default:
			sb_puts (&h->b, "~");
	}
}

static void hk_value (hk_t *h, u32 t, u64 off, uint indent, uint depth);

// Members of record T (parents first), each "name: value" at INDENT.
static void hk_record (hk_t *h, u32 t, u64 off, uint indent, uint depth, bool *any)
{
	if (!t || t >= h->n_types || depth > 48)
		return;
	const hk_type_t *y = h->types + t;
	if (y->parent)
		hk_record (h, y->parent, off, indent, depth + 1, any);
	for (u32 k = 0; k < y->n_m && !h->bad; k++)
	{
		const hk_member_t *m = y->m + k;
		u32 f, s, sub;
		hk_info (h, m->type, &f, &s, &sub);
		if ((f & 0x1f) < 2)
			continue; // void / opaque
		hk_ind (h, indent);
		sb_printf (&h->b, "%s:", m->name < h->n_fst ? h->fst[m->name] : "?");
		hk_value (h, m->type, off + m->offset, indent, depth + 1);
		*any = true;
	}
}

// " value\n", or "\n" + an indented block (INDENT + 2) for records and arrays of records.
static void hk_value (hk_t *h, u32 t, u64 off, uint indent, uint depth)
{
	u32 f, size, sub;
	hk_info (h, t, &f, &size, &sub);
	const uint k = f & 0x1f;
	if (depth > 64)
	{
		sb_puts (&h->b, " ~\n");
		return;
	}
	if (k == 7)
	{
		const size_t mark = h->b.n;
		sb_puts (&h->b, "\n");
		bool any = false;
		hk_record (h, t, off, indent + 2, depth, &any);
		if (!any)
		{
			h->b.n = mark;
			sb_puts (&h->b, " {}\n");
		}
		return;
	}
	if (k == 8)
	{
		u64 base = off;
		u32 count = f >> 8;
		if (!(f & 0x20))
		{
			// hkArray: item index of the elements
			const u64 idx = hk_u64 (h, off);
			if (idx >= h->n_items)
			{
				h->bad = true;
				return;
			}
			base = idx ? h->items[idx].off : 0;
			count = idx ? h->items[idx].count : 0;
		}
		u32 ef, es, esub;
		hk_info (h, sub, &ef, &es, &esub);
		if (!count)
		{
			sb_puts (&h->b, " []\n");
			return;
		}
		if (!es)
		{
			sb_puts (&h->b, " ~\n");
			return;
		}
		if (hk_is_scalar (h, sub, 0))
		{
			sb_puts (&h->b, " [");
			for (u32 i = 0; i < count && !h->bad; i++)
			{
				if (i)
					sb_puts (&h->b, ", ");
				hk_flow (h, sub, base + (u64)es * i, depth + 1);
			}
			sb_puts (&h->b, "]\n");
			return;
		}
		sb_puts (&h->b, "\n");
		for (u32 i = 0; i < count && !h->bad; i++)
		{
			hk_ind (h, indent + 2);
			sb_puts (&h->b, "-");
			hk_value (h, sub, base + (u64)es * i, indent + 2, depth + 1);
		}
		return;
	}
	sb_puts (&h->b, " ");
	hk_flow (h, t, off, depth);
	sb_puts (&h->b, "\n");
}

bool IsHavokTagFile (const u8 *d, size_t size)
{
	return d && size >= 16 && !memcmp (d + 4, "TAG0", 4) && (hk_be32 (d) & 0x3fffffff) <= size
		&& (hk_be32 (d) & 0x3fffffff) >= 16;
}

enumError ConvertHavokTagFileToYaml (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	if (text_size)
		*text_size = 0;
	hk_t h;
	if (!hk_parse (&h, d, size))
	{
		hk_free (&h);
		return ERR_INVALID_DATA;
	}
	size_t sn = 0;
	const u8 *sdk = hk_find (d + 8, d + (hk_be32 (d) & 0x3fffffff), "SDKV", &sn);
	sb_printf (&h.b, "# Havok tag file (SDK %.*s), %u types, %u items\nobjects:\n",
		sdk ? (int)sn : 1, sdk ? (ccp)sdk : "?", h.n_types, h.n_items);
	for (u32 i = 1; i < h.n_items && !h.bad; i++)
	{
		const hk_item_t *it = h.items + i;
		if (!(it->flags & 0x10))
			continue;
		sb_printf (&h.b, "  - item: %u\n    type: ", i);
		sb_quotez (&h.b, hk_type_name (&h, it->type));
		sb_puts (&h.b, "\n    value:");
		hk_value (&h, it->type, it->off, 4, 0);
	}
	const bool bad = h.bad || h.b.fail;
	hk_free (&h);
	if (bad)
	{
		FREE (h.b.d);
		return ERR_INVALID_DATA;
	}
	return sb_finish (&h.b, text, text_size);
}

bool IsPhiveCloth (const u8 *d, size_t size, size_t *tag_off, size_t *tag_size, size_t *aamp_off,
	size_t *aamp_size)
{
	if (!d || size < 0x30 || memcmp (d, "Phive\0", 6) || d[8] != 0xff || d[9] != 0xfe)
		return false;
	const u32 to = hk_le32 (d + 0x0c), ao = hk_le32 (d + 0x10), ts = hk_le32 (d + 0x18),
			  as = hk_le32 (d + 0x1c);
	if (to >= size || ts > size - to || !IsHavokTagFile (d + to, ts))
		return false;
	*tag_off = to;
	*tag_size = ts;
	*aamp_off = ao < size && as <= size - ao && as >= 4 && !memcmp (d + ao, "AAMP", 4) ? ao : 0;
	*aamp_size = *aamp_off ? as : 0;
	return true;
}
