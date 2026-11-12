// SPDX-License-Identifier: GPL-2.0+
// Animation event archives (BFFH ".baev") to YAML; see lib-baev.h.

#include "lib-baev.h"
#include "lib-sbuf.h"

typedef struct baev_t
{
	const u8 *d;
	size_t n;
	bool bad;
	u64 pool;
	sbuf_t b;
} baev_t;

static u32 bv32 (baev_t *v, u64 o)
{
	if (o > v->n || v->n - o < 4)
	{
		v->bad = true;
		return 0;
	}
	const u8 *p = v->d + o;
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static u64 bv64 (baev_t *v, u64 o)
{
	return bv32 (v, o) | (u64)bv32 (v, o + 4) << 32;
}

static float bvf (baev_t *v, u64 o)
{
	const u32 x = bv32 (v, o);
	float f;
	memcpy (&f, &x, 4);
	return f;
}

// Names are absolute offsets; pool-relative ones (smaller than the pool position) also occur.
static ccp bv_str (baev_t *v, u64 off)
{
	const u64 o = off >= v->pool ? off : v->pool + off;
	if (o >= v->n || !memchr (v->d + o, 0, v->n - o))
	{
		v->bad = true;
		return "";
	}
	return (ccp)v->d + o;
}

static void bv_float (baev_t *v, float f)
{
	if (f == (float)(s32)f && f > -1e9f && f < 1e9f)
		sb_printf (&v->b, "%d.0", (s32)f);
	else
		sb_printf (&v->b, "%.9g", f);
}

static u32 fnv1a (const u8 *s, size_t len)
{
	u32 h = 0x811c9dc5;
	for (size_t i = 0; i < len; i++)
		h = (h ^ s[i]) * 0x1000193;
	return h;
}

// Array header {u64 offset, u32 count, u32 entry size}; false (and bad) if it does not fit.
static bool bv_array (baev_t *v, u64 o, u64 *off, u32 *count, u32 want_size)
{
	*off = bv64 (v, o);
	*count = bv32 (v, o + 8);
	const u32 es = bv32 (v, o + 12);
	if (!*count)
		return true;
	if (es != want_size || *off > v->n || (u64)*count * es > v->n - *off)
	{
		v->bad = true;
		return false;
	}
	return true;
}

static void bv_params (baev_t *v, u64 o)
{
	u64 po;
	u32 pc;
	if (!bv_array (v, o, &po, &pc, 8))
		return;
	sb_puts (&v->b, "params: [");
	for (u32 i = 0; i < pc && !v->bad; i++)
	{
		const u64 p = bv64 (v, po + 8ull * i);
		const u32 type = bv32 (v, p);
		if (i)
			sb_puts (&v->b, ", ");
		switch (type)
		{
			case 0:
				sb_printf (&v->b, "%d", (s32)bv32 (v, p + 8));
				break;
			case 1:
				bv_float (v, bvf (v, p + 8));
				break;
			case 3:
				sb_puts (&v->b, "[");
				for (uint k = 0; k < 3; k++)
				{
					if (k)
						sb_puts (&v->b, ", ");
					bv_float (v, bvf (v, p + 8 + 4 * k));
				}
				sb_puts (&v->b, "]");
				break;
			case 5:
				sb_quotez (&v->b, bv_str (v, bv64 (v, p + 8)));
				break;
			default:
				sb_printf (&v->b, "{type: %u, raw: 0x%08x}", type, bv32 (v, p + 8));
		}
	}
	sb_puts (&v->b, "]");
}

static void bv_node (baev_t *v, u64 no, uint ind)
{
	static const char sp[] = "                ";
	u64 eo;
	u32 ec;
	if (!bv_array (v, no, &eo, &ec, 0x30))
		return;
	sb_printf (&v->b, "%.*snode-hash: 0x%08x\n", ind, sp, bv32 (v, no + 16));
	const u32 unk = bv32 (v, no + 20);
	if (unk)
		sb_printf (&v->b, "%.*sunknown: 0x%x\n", ind, sp, unk);
	if (!ec)
		return;
	sb_printf (&v->b, "%.*sevents:\n", ind, sp);
	for (u32 i = 0; i < ec && !v->bad; i++)
	{
		const u64 e = eo + 0x30ull * i;
		sb_printf (&v->b, "%.*s  - name: ", ind, sp);
		sb_quotez (&v->b, bv_str (v, bv64 (v, e)));
		sb_puts (&v->b, "\n");
		const u32 hold = bv32 (v, e + 0x28), id = bv32 (v, e + 0x2c);
		if (hold)
			sb_printf (&v->b, "%.*s    hold: true\n", ind, sp);
		if (id)
			sb_printf (&v->b, "%.*s    id: %u\n", ind, sp, id);
		for (uint kind = 0; kind < 2 && !v->bad; kind++)
		{
			u64 ao;
			u32 ac;
			if (!bv_array (v, e + 8 + 0x10 * kind, &ao, &ac, 0x18) || !ac)
				continue;
			sb_printf (&v->b, "%.*s    %s:\n", ind, sp, kind ? "holds" : "triggers");
			for (u32 k = 0; k < ac && !v->bad; k++)
			{
				const u64 t = ao + 0x18ull * k;
				sb_printf (&v->b, "%.*s      - {%s: ", ind, sp, kind ? "start" : "frame");
				bv_float (v, bvf (v, t + 16));
				if (kind)
				{
					sb_puts (&v->b, ", end: ");
					bv_float (v, bvf (v, t + 20));
				}
				sb_puts (&v->b, ", ");
				bv_params (v, t);
				sb_puts (&v->b, "}\n");
			}
		}
	}
}

typedef struct bv_name_t
{
	u32 hash;
	u32 off;
	u32 len;
} bv_name_t;

static int bv_name_cmp (const void *a, const void *b)
{
	const u32 x = ((const bv_name_t *)a)->hash, y = ((const bv_name_t *)b)->hash;
	return x < y ? -1 : x > y;
}

bool IsBaev (const u8 *d, size_t size)
{
	return d && size >= 0xa8 && !memcmp (d, "BFFH", 4) && size >= 0x78
		&& !memcmp (d + 0x28, "Nintendo.AnimationEvent.", 24);
}

enumError ConvertBaevToYaml (
	const u8 *d, size_t size, const u8 *names, size_t names_size, char **text, size_t *text_size)
{
	*text = 0;
	if (text_size)
		*text_size = 0;
	if (!IsBaev (d, size))
		return ERR_INVALID_DATA;
	baev_t v = { 0 };
	v.d = d;
	v.n = size;

	// sections: find "Default" (container) and "StringPool"
	u64 so;
	u32 sc;
	if (!bv_array (&v, 0x10, &so, &sc, 0x28) || !sc)
		return ERR_INVALID_DATA;
	u64 base = 0;
	for (u32 i = 0; i < sc; i++)
	{
		const u64 s = so + 0x28ull * i;
		if (memcmp (d + s, "BFSI", 4))
			return ERR_INVALID_DATA;
		if (!strncmp ((ccp)d + s + 0x18, "Default", 16))
			base = bv64 (&v, s + 0x10);
	}
	if (!base || v.bad)
		return ERR_INVALID_DATA;
	v.pool = bv64 (&v, base + 0x10);

	// optional name candidates: printable runs of 2..255 bytes
	bv_name_t *nm = 0;
	size_t n_nm = 0, cap = 0;
	for (size_t i = 0; names && i < names_size;)
	{
		size_t j = i;
		while (j < names_size && names[j] >= 0x20 && names[j] < 0x7f && j - i < 255)
			j++;
		if (j - i >= 2)
		{
			if (n_nm == cap)
			{
				cap = cap ? cap * 2 : 1024;
				bv_name_t *nn = REALLOC (nm, cap * sizeof (*nn));
				if (!nn)
					break;
				nm = nn;
			}
			nm[n_nm].hash = fnv1a (names + i, j - i);
			nm[n_nm].off = (u32)i;
			nm[n_nm++].len = (u32)(j - i);
		}
		i = j + 1;
	}
	if (n_nm)
		qsort (nm, n_nm, sizeof (*nm), bv_name_cmp);

	if (base + 0x38 > size)
	{
		FREE (nm);
		return ERR_INVALID_DATA;
	}
	sb_printf (&v.b, "# Animation event archive (%.80s)\nversion: %u.%u.%u\n", (ccp)d + 0x28,
		(u32)(d[base + 10] | d[base + 11] << 8), d[base + 9], d[base + 8]);
	u64 ao, no;
	u32 ac, nc;
	bv_array (&v, base + 0x18, &ao, &ac, 0x18);
	bv_array (&v, base + 0x28, &no, &nc, 0x18);
	if (v.bad)
	{
		FREE (nm);
		FREE (v.b.d);
		return ERR_INVALID_DATA;
	}
	bool *used = CALLOC (nc ? nc : 1, 1);
	uint resolved = 0;
	sb_printf (&v.b, "animations: # %u\n", ac);
	for (u32 i = 0; i < ac && !v.bad; i++)
	{
		const u64 a = ao + 0x18ull * i;
		const u32 h = bv32 (&v, a);
		const bv_name_t key = { h, 0, 0 };
		const bv_name_t *hit = n_nm ? bsearch (&key, nm, n_nm, sizeof (*nm), bv_name_cmp) : 0;
		sb_puts (&v.b, "  - name: ");
		if (hit)
		{
			sb_quote (&v.b, (ccp)names + hit->off, hit->len);
			resolved++;
		}
		else
			sb_puts (&v.b, "~");
		sb_printf (&v.b, "\n    hash: 0x%08x\n", h);
		u64 io;
		u32 ic;
		if (!bv_array (&v, a + 8, &io, &ic, 4))
			break;
		for (u32 k = 0; k < ic && !v.bad; k++)
		{
			const u32 idx = bv32 (&v, io + 4ull * k);
			if (idx >= nc)
			{
				v.bad = true;
				break;
			}
			used[idx] = true;
			sb_printf (&v.b, "    %s- node: %u\n", k ? "" : "nodes:\n    ", idx);
			bv_node (&v, no + 0x18ull * idx, 6);
		}
	}
	bool any = false;
	for (u32 i = 0; i < nc && !v.bad; i++)
		if (!used[i])
		{
			if (!any)
				sb_puts (&v.b, "unreferenced-nodes:\n");
			any = true;
			sb_printf (&v.b, "  - node: %u\n", i);
			bv_node (&v, no + 0x18ull * i, 4);
		}
	if (n_nm)
		sb_printf (&v.b, "# %u of %u animation names resolved\n", resolved, ac);
	FREE (used);
	FREE (nm);
	if (v.bad)
	{
		FREE (v.b.d);
		return ERR_INVALID_DATA;
	}
	return sb_finish (&v.b, text, text_size);
}
