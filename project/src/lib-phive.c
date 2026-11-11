// SPDX-License-Identifier: GPL-2.0+
// Phive collision shapes (.bphsh) -> OBJ + material JSON. See lib-phive.h.
//
// Layout of the stored hknpMeshShape (all fields little endian, offsets from the start of the shape):
//   0x1C u8  numShapeKeyBits            0x60 float[4] vertex bitScale16Inv
//   0x70 relview<ShapeTagTableEntry>    0x90 relview<GeometrySection>
// relview = { s32 offset (from the view itself), s32 count }. GeometrySection (0x40 bytes):
//   +0x08 relview<Primitive{a,b,c,d : u8}>  +0x10 relview<Vertex16{x,y,z : u16}>  +0x20 u32 sectionOffset[3]
// A primitive is a triangle (c == d) or a quad (a,b,c + a,c,d). ShapeTagTableEntry { u32 meshPrimitiveKey,
// u16 shapeTag, u16 pad } is sorted by key; the tag is the index into the material tables.

#include "lib-phive.h"
#include "phive-tables.inc"
#include <stdio.h>
#include <math.h>

#define PHIVE_COUNT(a) ((uint)(sizeof (a) / sizeof ((a)[0])))

static u32 p_le32 (const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static u16 p_le16 (const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static u32 p_be32 (const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static float p_lef32 (const u8 *p)
{
	const u32 v = p_le32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

// ----------------------------------------------------------------------------------------------------------------

typedef struct sbuf_t
{
	char *d;
	size_t n, cap;
	bool fail;
} sbuf_t;

static void sb_printf (sbuf_t *b, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
static void sb_printf (sbuf_t *b, const char *fmt, ...)
{
	if (b->fail)
		return;
	char tmp[512];
	va_list ap;
	va_start (ap, fmt);
	const int len = vsnprintf (tmp, sizeof (tmp), fmt, ap);
	va_end (ap);
	if (len < 0 || (size_t)len >= sizeof (tmp))
	{
		b->fail = true;
		return;
	}
	if (b->n + len + 1 > b->cap)
	{
		size_t cap = b->cap ? b->cap * 2 : 1 << 16;
		while (cap < b->n + len + 1)
			cap *= 2;
		char *nd = REALLOC (b->d, cap);
		if (!nd)
		{
			b->fail = true;
			return;
		}
		b->d = nd;
		b->cap = cap;
	}
	memcpy (b->d + b->n, tmp, len);
	b->n += len;
	b->d[b->n] = 0;
}

// ----------------------------------------------------------------------------------------------------------------

bool IsPhiveFile (const u8 *data, size_t size)
{
	return size >= 0x30 && !memcmp (data, "Phive\0", 6);
}

// target of a relative view located at 'view' in the blob; 0 if it leaves the blob
static bool p_view (const u8 *blob, size_t blob_n, size_t view, size_t elem, size_t *start, u32 *count)
{
	if (view + 8 > blob_n)
		return false;
	const s32 off = (s32)p_le32 (blob + view);
	const s32 cnt = (s32)p_le32 (blob + view + 4);
	const s64 t = (s64)view + off;
	if (cnt < 0 || t < 0 || (u64)t > blob_n || (u64)cnt * elem > blob_n - (u64)t)
		return false;
	*start = (size_t)t;
	*count = (u32)cnt;
	return true;
}

static int p_bit_width (int v)
{
	int bits = 0;
	if (v <= 0)
		return 0;
	while ((1 << bits) <= v)
		bits++;
	return bits;
}

typedef struct tri_t
{
	u32 a, b, c;
	u16 mat;
} tri_t;

static int p_cmp_key (const void *a, const void *b)
{
	return strcmp (*(const char *const *)a, *(const char *const *)b);
}

enumError ConvertPhiveToObj (
	const u8 *data, size_t size, char **obj_out, size_t *obj_size, char **json_out, size_t *json_size)
{
	if (!IsPhiveFile (data, size))
		return ERR_WRONG_FILE_TYPE;
	const u32 hkt_off = p_le32 (data + 12), tab0_off = p_le32 (data + 16), tab1_off = p_le32 (data + 20),
			  hkt_size = p_le32 (data + 28), tab0_size = p_le32 (data + 32), tab1_size = p_le32 (data + 36);
	if ((u64)hkt_off + hkt_size > size || hkt_size < 0x20 || memcmp (data + hkt_off + 4, "TAG0", 4))
		return ERR_INVALID_DATA;

	// walk the tag file sections to the DATA section holding the shape
	const u8 *hkt = data + hkt_off;
	size_t blob_off = 0, blob_n = 0;
	u32 sdk_year = 0; // "SDKV" section: Havok version as "YYYYMMDD" text
	for (size_t p = 8; p + 8 <= hkt_size;)
	{
		const u32 sz = p_be32 (hkt + p) & 0x3fffffff;
		if (sz < 8 || p + sz > hkt_size)
			break;
		if (!memcmp (hkt + p + 4, "SDKV", 4) && sz >= 16)
			for (int i = 0; i < 4; i++)
				sdk_year = sdk_year * 10 + (hkt[p + 8 + i] >= '0' && hkt[p + 8 + i] <= '9' ? hkt[p + 8 + i] - '0' : 0);
		if (!memcmp (hkt + p + 4, "DATA", 4))
		{
			blob_off = p + 8;
			blob_n = sz - 8;
			break;
		}
		p += sz;
	}
	if (!blob_n || blob_n < 0xA0)
		return ERR_INVALID_DATA;
	const u8 *blob = hkt + blob_off;

	// material tables: {s32 matId, s32 pad, u64 flags} and u64 collision flags, one entry per material
	const uint n_mat = (uint)(tab0_size / 16 < tab1_size / 8 ? tab0_size / 16 : tab1_size / 8);
	if ((u64)tab0_off + (u64)n_mat * 16 > size || (u64)tab1_off + (u64)n_mat * 8 > size)
		return ERR_INVALID_DATA;
	const u8 *tab0 = data + tab0_off, *tab1 = data + tab1_off;

	const int key_bits = blob[0x1C];
	float inv[3];
	for (int i = 0; i < 3; i++)
		inv[i] = p_lef32 (blob + 0x60 + 4 * i);

	size_t sec_start, tag_start;
	u32 n_sec, n_tag;
	if (!p_view (blob, blob_n, 0x90, 0x40, &sec_start, &n_sec) || !p_view (blob, blob_n, 0x70, 8, &tag_start, &n_tag)
		|| !n_tag || n_sec > 0x100000)
		return ERR_INVALID_DATA;
	// Splatoon 3 (Havok 2021.1) stores the vertex count in the vertex view; Havok 2023.2 files (Mario Kart World, ...) store its size in bytes
	const bool vert_bytes = sdk_year >= 2022;
	const int section_shift = key_bits - p_bit_width ((int)n_sec - 1);
	if (section_shift < 1 || section_shift > 31)
		return ERR_INVALID_DATA;

	// collect vertices (3 per triangle, like the game's surface geometry) and triangles
	size_t v_cap = 1 << 16, v_n = 0, t_cap = 1 << 15, t_n = 0;
	float *verts = MALLOC (v_cap * 3 * sizeof (float));
	tri_t *tris = MALLOC (t_cap * sizeof (tri_t));
	if (!verts || !tris)
	{
		FREE (verts);
		FREE (tris);
		return ERR_OUT_OF_MEMORY;
	}
	enumError err = ERR_OK;

	for (u32 s = 0; s < n_sec && !err; s++)
	{
		const size_t sec = sec_start + (size_t)s * 0x40;
		size_t prim_start, vert_start;
		u32 n_prim, n_vert;
		if (!p_view (blob, blob_n, sec + 0x08, 4, &prim_start, &n_prim)
			|| !p_view (blob, blob_n, sec + 0x10, vert_bytes ? 1 : 6, &vert_start, &n_vert))
		{
			err = ERR_INVALID_DATA;
			break;
		}
		const s32 so[3] = { (s32)p_le32 (blob + sec + 0x20), (s32)p_le32 (blob + sec + 0x24), (s32)p_le32 (blob + sec + 0x28) };
		// A section offset of INT_MAX marks a section that keeps its vertices as plain float32 x/y/z.
		const bool is_float = vert_bytes && so[0] == 0x7fffffff;
		if (vert_bytes) // the last section may carry a little padding after its vertices
			n_vert /= is_float ? 12 : 6;
		for (u32 p = 0; p < n_prim; p++)
		{
			const u8 *pr = blob + prim_start + 4 * (size_t)p;
			if (pr[0] >= n_vert || pr[1] >= n_vert || pr[2] >= n_vert || pr[3] >= n_vert)
			{
				err = ERR_INVALID_DATA;
				break;
			}
			float pv[4][3];
			for (int k = 0; k < 4; k++)
			{
				const u8 *q = blob + vert_start + (is_float ? 12 : 6) * (size_t)pr[k];
				for (int i = 0; i < 3; i++)
					pv[k][i] = is_float ? p_lef32 (q + 4 * i)
						: (float)((int)p_le16 (q + 2 * i) + so[i]) * inv[i];
			}

			// material: last tag table entry whose key is <= the primitive key
			const u32 key = ((u32)s << section_shift) | ((u32)p << 1);
			int lo = 0, hi = (int)n_tag - 1;
			while (lo < hi)
			{
				const int mid = (lo + hi + 1) / 2;
				if (p_le32 (blob + tag_start + 8 * (size_t)mid) <= key)
					lo = mid;
				else
					hi = mid - 1;
			}
			const u16 mat = p_le16 (blob + tag_start + 8 * (size_t)lo + 4);

			const int n_tri = pr[2] != pr[3] ? 2 : 1;
			for (int t = 0; t < n_tri; t++)
			{
				if (t_n + 1 > t_cap || v_n + 3 > v_cap)
				{
					if (t_n + 1 > t_cap)
						t_cap *= 2;
					if (v_n + 3 > v_cap)
						v_cap *= 2;
					float *nv = REALLOC (verts, v_cap * 3 * sizeof (float));
					tri_t *nt = REALLOC (tris, t_cap * sizeof (tri_t));
					if (nv)
						verts = nv;
					if (nt)
						tris = nt;
					if (!nv || !nt)
					{
						err = ERR_OUT_OF_MEMORY;
						break;
					}
				}
				const int idx[3] = { 0, 1 + t, 2 + t };
				for (int k = 0; k < 3; k++)
					memcpy (verts + 3 * (v_n + k), pv[idx[k]], 3 * sizeof (float));
				tris[t_n].a = (u32)v_n;
				tris[t_n].b = (u32)v_n + 1;
				tris[t_n].c = (u32)v_n + 2;
				tris[t_n].mat = mat;
				t_n++;
				v_n += 3;
			}
			if (err)
				break;
		}
	}

	sbuf_t ob = { 0 }, jb = { 0 };
	if (!err)
	{
		for (size_t i = 0; i < v_n; i++)
			sb_printf (&ob, "v %.6f %.6f %.6f\n", verts[3 * i], verts[3 * i + 1], verts[3 * i + 2]);

		// one group per material entry; names are "<material name><running number per name>"
		char **keys = CALLOC (n_mat ? n_mat : 1, sizeof (*keys));
		uint *midx = CALLOC (n_mat ? n_mat : 1, sizeof (*midx));
		uint *used = CALLOC (PHIVE_COUNT (phive_mat_names) + 1, sizeof (*used));
		if (!keys || !midx || !used)
			err = ERR_OUT_OF_MEMORY;
		for (uint m = 0; m < n_mat && !err; m++)
		{
			const s32 id = (s32)p_le32 (tab0 + 16 * (size_t)m);
			const uint slot = id >= 0 && (uint)id < PHIVE_COUNT (phive_mat_names) ? (uint)id : PHIVE_COUNT (phive_mat_names);
			const char *name = slot < PHIVE_COUNT (phive_mat_names) ? phive_mat_names[slot] : "UNDEFINED";
			char key[300];
			snprintf (key, sizeof (key), "%s%02u", name, used[slot]++);
			keys[m] = STRDUP (key);
			midx[m] = m;
			sb_printf (&ob, "o %s\nusemtl %s\n", key, key);
			for (size_t t = 0; t < t_n; t++)
				if (tris[t].mat == m)
					sb_printf (&ob, "f %u %u %u\n", tris[t].a + 1, tris[t].b + 1, tris[t].c + 1);
		}

		// JSON like nlohmann::json::dump(4): object keys sorted, array items one per line
		if (!err)
		{
			char **sorted = CALLOC (n_mat ? n_mat : 1, sizeof (*sorted));
			if (!sorted)
				err = ERR_OUT_OF_MEMORY;
			for (uint m = 0; m < n_mat && !err; m++)
				sorted[m] = keys[m];
			if (!err)
			{
				qsort (sorted, n_mat, sizeof (*sorted), p_cmp_key);
				sb_printf (&jb, "{");
				for (uint k = 0; k < n_mat; k++)
				{
					uint m = 0;
					while (m < n_mat && keys[m] != sorted[k])
						m++;
					const s32 id = (s32)p_le32 (tab0 + 16 * (size_t)m);
					const u64 flags = (u64)p_le32 (tab0 + 16 * (size_t)m + 8) | (u64)p_le32 (tab0 + 16 * (size_t)m + 12) << 32;
					const u64 col = (u64)p_le32 (tab1 + 8 * (size_t)m) | (u64)p_le32 (tab1 + 8 * (size_t)m + 4) << 32;
					const char *name = id >= 0 && (uint)id < PHIVE_COUNT (phive_mat_names) ? phive_mat_names[id] : "UNDEFINED";
					sb_printf (&jb, "%s\n    \"%s\": {\n", k ? "," : "", sorted[k]);
					for (int pass = 0; pass < 2; pass++)
					{
						sb_printf (&jb, "        \"%s\": [", pass ? "mat_flags" : "col_disable_flags");
						uint cnt = 0;
						for (uint i = 0; i < 64; i++)
						{
							const bool hit = pass ? (flags >> i & 1) != 0 : (col >> i & 1) == 0;
							const uint n_names = pass ? PHIVE_COUNT (phive_mat_flag_names) : PHIVE_COUNT (phive_col_flag_names);
							if (!hit || i >= n_names)
								continue;
							sb_printf (&jb, "%s\n            \"%s\"", cnt++ ? "," : "",
								pass ? phive_mat_flag_names[i] : phive_col_flag_names[i]);
						}
						sb_printf (&jb, "%s],\n", cnt ? "\n        " : "");
					}
					sb_printf (&jb, "        \"mat_name\": \"%s\"\n    }", name);
				}
				sb_printf (&jb, "%s}", n_mat ? "\n" : "");
			}
			FREE (sorted);
		}
		for (uint m = 0; keys && m < n_mat; m++)
			FREE (keys[m]);
		FREE (keys);
		FREE (midx);
		FREE (used);
	}
	FREE (verts);
	FREE (tris);
	if (!err && (ob.fail || jb.fail))
		err = ERR_OUT_OF_MEMORY;
	if (err)
	{
		FREE (ob.d);
		FREE (jb.d);
		return err;
	}
	*obj_out = ob.d ? ob.d : STRDUP ("");
	*obj_size = ob.n;
	*json_out = jb.d;
	*json_size = jb.n;
	return ERR_OK;
}

// ----------------------------------------------------------------------------------------------------------------
// Static compound bodies (.bphsc): Phive container version 2.2 ("Phive\0" 02 02, BOM, then a u32 section offset
// table at 0x0C). Sections that start with a Havok tag file ("TAG0") hold an hknpCompoundShape. The tag file's
// INDX/ITEM table lists every object in its DATA section {u32 flags << 24 | type index, u32 offset, u32 count};
// TYPE/TNA1 names the types (varint encoded, names in TST1). Object layouts (Havok 2023.2, offsets in the object):
//   hknpShape:          +0x18 u8 shape type, +0x20 f32 convex radius, +0x30 relarray properties
//   hknpCompoundShape:  +0x48 s32 relative offset of the instance array, +0x50 u32 instance count,
//                       +0x70/+0x80 world AABB; instance (64 bytes): quaternion, translation (+ u32 flags),
//                       scale (+ u16 id, u16), s64 relative pointer to the instanced shape (0 = free slot)
//   hknpMeshShape:      as in .bphsh (see above)
//   convex shapes:      +0x40 relarray<hkFloat3> vertices, +0x50 relarray<Face{u16 first, u8 count, u8}>,
//                       +0x58 relarray<u8> face vertex indices; capsules keep their two end points at +0x40
// relarray = { s32 offset from the field, s32 count }.

typedef struct p_item_t
{
	u32 type, off, count;
} p_item_t;

typedef struct p_tag_t
{
	const u8 *data;
	size_t data_n;
	ccp *names; // per type index, NULL if unknown
	uint n_names;
	p_item_t *items;
	uint n_items;
	bool vert_bytes;
} p_tag_t;

typedef struct p_out_t
{
	sbuf_t *b;
	size_t n_vert;
	uint n_shapes, n_mesh, n_convex, n_capsule, n_other;
} p_out_t;

typedef struct p_xform_t
{
	float m[3][3], t[3];
} p_xform_t;

static bool p_varint (const u8 *b, size_t n, size_t *i, u32 *out)
{
	if (*i >= n)
		return false;
	const u8 x = b[*i];
	uint len = x < 0x80 ? 1 : x < 0xc0 ? 2 : x < 0xe0 ? 3 : x < 0xe8 ? 4 : 0;
	if (!len || *i + len > n)
		return false;
	u32 v = len == 1 ? x : len == 2 ? x & 0x3f : len == 3 ? x & 0x1f : x & 0x07;
	for (uint k = 1; k < len; k++)
		v = v << 8 | b[*i + k];
	*i += len;
	*out = v;
	return true;
}

static void p_tag_free (p_tag_t *t)
{
	FREE (t->names);
	FREE (t->items);
	memset (t, 0, sizeof (*t));
}

// Parses the tag file at HKT (SIZE bytes).
static bool p_tag_parse (const u8 *hkt, size_t size, p_tag_t *t)
{
	memset (t, 0, sizeof (*t));
	if (size < 16 || memcmp (hkt + 4, "TAG0", 4))
		return false;
	const size_t end = p_be32 (hkt) & 0x3fffffff;
	if (end > size)
		return false;
	const u8 *tst = 0, *tna = 0, *item = 0;
	size_t tst_n = 0, tna_n = 0, item_n = 0;
	u32 sdk_year = 0;
	for (size_t p = 8; p + 8 <= end;)
	{
		const u32 sz = p_be32 (hkt + p) & 0x3fffffff;
		if (sz < 8 || p + sz > end)
			break;
		const u8 *tag = hkt + p + 4;
		if (!memcmp (tag, "SDKV", 4) && sz >= 16)
			for (int i = 0; i < 4; i++)
				sdk_year = sdk_year * 10 + (hkt[p + 8 + i] >= '0' && hkt[p + 8 + i] <= '9' ? hkt[p + 8 + i] - '0' : 0);
		else if (!memcmp (tag, "DATA", 4))
			t->data = hkt + p + 8, t->data_n = sz - 8;
		else if (!memcmp (tag, "TYPE", 4) || !memcmp (tag, "INDX", 4))
			for (size_t q = p + 8; q + 8 <= p + sz;)
			{
				const u32 s2 = p_be32 (hkt + q) & 0x3fffffff;
				if (s2 < 8 || q + s2 > p + sz)
					break;
				const u8 *t2 = hkt + q + 4;
				if (!memcmp (t2, "TST1", 4))
					tst = hkt + q + 8, tst_n = s2 - 8;
				else if (!memcmp (t2, "TNA1", 4))
					tna = hkt + q + 8, tna_n = s2 - 8;
				else if (!memcmp (t2, "ITEM", 4))
					item = hkt + q + 8, item_n = s2 - 8;
				q += s2;
			}
		p += sz;
	}
	if (!t->data || !tst || !tna || !item)
		return false;
	t->vert_bytes = sdk_year >= 2022;

	// type name strings
	uint n_str = 0;
	for (size_t i = 0; i < tst_n; i++)
		n_str += !tst[i];
	ccp *strs = CALLOC (n_str + 1, sizeof (*strs));
	if (!strs)
		return false;
	uint k = 0;
	for (size_t i = 0; i < tst_n && k < n_str; k++)
	{
		strs[k] = (ccp)tst + i;
		i += strnlen ((ccp)tst + i, tst_n - i) + 1;
	}

	size_t i = 0;
	u32 n_types = 0;
	bool ok = p_varint (tna, tna_n, &i, &n_types) && n_types < 0x10000;
	if (ok)
		ok = (t->names = CALLOC (n_types + 1, sizeof (*t->names))) != 0;
	for (u32 ti = 1; ok && ti < n_types; ti++)
	{
		u32 name, n_tmpl, a, b;
		ok = p_varint (tna, tna_n, &i, &name) && p_varint (tna, tna_n, &i, &n_tmpl);
		for (u32 x = 0; ok && x < n_tmpl; x++)
			ok = p_varint (tna, tna_n, &i, &a) && p_varint (tna, tna_n, &i, &b);
		if (ok && name < n_str)
			t->names[ti] = strs[name];
	}
	t->n_names = n_types;
	FREE (strs);
	if (!ok)
	{
		p_tag_free (t);
		return false;
	}

	t->n_items = (uint)(item_n / 12);
	t->items = CALLOC (t->n_items + 1, sizeof (*t->items));
	if (!t->items)
	{
		p_tag_free (t);
		return false;
	}
	for (uint n = 0; n < t->n_items; n++)
	{
		const u32 f = p_le32 (item + 12 * n);
		t->items[n].type = f & 0xffffff;
		t->items[n].off = p_le32 (item + 12 * n + 4);
		t->items[n].count = p_le32 (item + 12 * n + 8);
	}
	return true;
}

static ccp p_tag_type_at (const p_tag_t *t, size_t off)
{
	for (uint n = 0; n < t->n_items; n++)
		if (t->items[n].off == off && t->items[n].type && t->items[n].type < t->n_names && t->items[n].count)
			return t->names[t->items[n].type];
	return 0;
}

static void p_apply (const p_xform_t *x, const float *v, float *o)
{
	for (int r = 0; r < 3; r++)
		o[r] = x->m[r][0] * v[0] + x->m[r][1] * v[1] + x->m[r][2] * v[2] + x->t[r];
}

static void p_emit_v (p_out_t *o, const p_xform_t *x, const float *v)
{
	float w[3];
	p_apply (x, v, w);
	sb_printf (o->b, "v %.6f %.6f %.6f\n", w[0], w[1], w[2]);
	o->n_vert++;
}

static void p_compound_mesh (const p_tag_t *t, size_t m, const p_xform_t *x, p_out_t *o)
{
	const u8 *blob = t->data;
	const size_t n = t->data_n;
	size_t sec_start, tag_start;
	u32 n_sec, n_tag;
	if (m + 0xa0 > n || !p_view (blob, n, m + 0x90, 0x40, &sec_start, &n_sec) || !p_view (blob, n, m + 0x70, 8, &tag_start, &n_tag)
		|| !n_tag || n_sec > 0x100000)
		return;
	const int key_bits = blob[m + 0x1c];
	const int section_shift = key_bits - p_bit_width ((int)n_sec - 1);
	if (section_shift < 1 || section_shift > 31)
		return;
	float inv[3];
	for (int i = 0; i < 3; i++)
		inv[i] = p_lef32 (blob + m + 0x60 + 4 * i);
	int last_mat = -1;
	for (u32 s = 0; s < n_sec; s++)
	{
		const size_t sec = sec_start + (size_t)s * 0x40;
		size_t prim_start, vert_start;
		u32 n_prim, n_vert;
		if (!p_view (blob, n, sec + 0x08, 4, &prim_start, &n_prim) || !p_view (blob, n, sec + 0x10, t->vert_bytes ? 1 : 6, &vert_start, &n_vert))
			return;
		const s32 so[3] = { (s32)p_le32 (blob + sec + 0x20), (s32)p_le32 (blob + sec + 0x24), (s32)p_le32 (blob + sec + 0x28) };
		const bool is_float = t->vert_bytes && so[0] == 0x7fffffff;
		if (t->vert_bytes)
			n_vert /= is_float ? 12 : 6;
		for (u32 p = 0; p < n_prim; p++)
		{
			const u8 *pr = blob + prim_start + 4 * (size_t)p;
			if (pr[0] >= n_vert || pr[1] >= n_vert || pr[2] >= n_vert || pr[3] >= n_vert)
				return;
			const u32 key = ((u32)s << section_shift) | ((u32)p << 1);
			int lo = 0, hi = (int)n_tag - 1;
			while (lo < hi)
			{
				const int mid = (lo + hi + 1) / 2;
				if (p_le32 (blob + tag_start + 8 * (size_t)mid) <= key)
					lo = mid;
				else
					hi = mid - 1;
			}
			const int mat = p_le16 (blob + tag_start + 8 * (size_t)lo + 4);
			if (mat != last_mat)
				sb_printf (o->b, "usemtl material_%d\n", last_mat = mat);
			const size_t base = o->n_vert;
			for (int k = 0; k < 4; k++)
			{
				const u8 *q = blob + vert_start + (is_float ? 12 : 6) * (size_t)pr[k];
				float v[3];
				for (int i = 0; i < 3; i++)
					v[i] = is_float ? p_lef32 (q + 4 * i) : (float)((int)p_le16 (q + 2 * i) + so[i]) * inv[i];
				p_emit_v (o, x, v);
			}
			sb_printf (o->b, "f %zu %zu %zu\n", base + 1, base + 2, base + 3);
			if (pr[2] != pr[3])
				sb_printf (o->b, "f %zu %zu %zu\n", base + 1, base + 3, base + 4);
		}
	}
	o->n_mesh++;
}

// Convex polytope: faces as fans over their vertex indices.
static bool p_compound_convex (const p_tag_t *t, size_t c, const p_xform_t *x, p_out_t *o)
{
	size_t vs, fs, is;
	u32 nv, nf, ni;
	if (!p_view (t->data, t->data_n, c + 0x40, 12, &vs, &nv) || !p_view (t->data, t->data_n, c + 0x50, 4, &fs, &nf)
		|| !p_view (t->data, t->data_n, c + 0x58, 1, &is, &ni) || !nv || !nf || !ni)
		return false;
	const size_t base = o->n_vert;
	for (u32 i = 0; i < nv; i++)
	{
		float v[3];
		for (int k = 0; k < 3; k++)
			v[k] = p_lef32 (t->data + vs + 12 * (size_t)i + 4 * k);
		p_emit_v (o, x, v);
	}
	for (u32 f = 0; f < nf; f++)
	{
		const uint first = p_le16 (t->data + fs + 4 * (size_t)f), cnt = t->data[fs + 4 * (size_t)f + 2];
		if (first + cnt > ni || cnt < 3)
			continue;
		const u8 *ix = t->data + is + first;
		for (uint k = 1; k + 1 < cnt; k++)
			if (ix[0] < nv && ix[k] < nv && ix[k + 1] < nv)
				sb_printf (o->b, "f %zu %zu %zu\n", base + 1 + ix[0], base + 1 + ix[k], base + 1 + ix[k + 1]);
	}
	o->n_convex++;
	return true;
}

// Capsule (two end points and the convex radius) as an 8-sided tube with pointed caps.
static bool p_compound_capsule (const p_tag_t *t, size_t c, const p_xform_t *x, p_out_t *o)
{
	size_t vs;
	u32 nv;
	if (!p_view (t->data, t->data_n, c + 0x40, 12, &vs, &nv) || nv != 2)
		return false;
	float a[3], b[3], ax[3];
	for (int k = 0; k < 3; k++)
	{
		a[k] = p_lef32 (t->data + vs + 4 * k);
		b[k] = p_lef32 (t->data + vs + 12 + 4 * k);
		ax[k] = b[k] - a[k];
	}
	const float r = p_lef32 (t->data + c + 0x20);
	float len = sqrtf (ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
	if (len < 1e-6f)
		ax[0] = 0, ax[1] = 1, ax[2] = 0, len = 0;
	else
		for (int k = 0; k < 3; k++)
			ax[k] /= len;
	// two unit vectors perpendicular to the axis: u = axis x helper, w = axis x u
	const float h[3] = { fabsf (ax[1]) > 0.9f ? 1.0f : 0.0f, fabsf (ax[1]) > 0.9f ? 0.0f : 1.0f, 0 };
	float u[3] = { ax[1] * h[2] - ax[2] * h[1], ax[2] * h[0] - ax[0] * h[2], ax[0] * h[1] - ax[1] * h[0] };
	const float ul = sqrtf (u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
	for (int k = 0; k < 3; k++)
		u[k] /= ul;
	const float w[3] = { ax[1] * u[2] - ax[2] * u[1], ax[2] * u[0] - ax[0] * u[2], ax[0] * u[1] - ax[1] * u[0] };
	const size_t base = o->n_vert;
	for (int ring = 0; ring < 2; ring++)
		for (int s = 0; s < 8; s++)
		{
			const float ang = (float)s * 0.785398163f, cs = cosf (ang) * r, sn = sinf (ang) * r;
			float v[3];
			for (int k = 0; k < 3; k++)
				v[k] = (ring ? b[k] : a[k]) + u[k] * cs + w[k] * sn;
			p_emit_v (o, x, v);
		}
	float tip[3];
	for (int k = 0; k < 3; k++)
		tip[k] = a[k] - ax[k] * r;
	p_emit_v (o, x, tip);
	for (int k = 0; k < 3; k++)
		tip[k] = b[k] + ax[k] * r;
	p_emit_v (o, x, tip);
	for (int s = 0; s < 8; s++)
	{
		const size_t a0 = base + 1 + s, a1 = base + 1 + (s + 1) % 8, b0 = a0 + 8, b1 = a1 + 8;
		sb_printf (o->b, "f %zu %zu %zu\nf %zu %zu %zu\n", a0, a1, b1, a0, b1, b0);
		sb_printf (o->b, "f %zu %zu %zu\nf %zu %zu %zu\n", base + 17, a1, a0, base + 18, b0, b1);
	}
	o->n_capsule++;
	return true;
}

static void p_compound_shape (const p_tag_t *t, size_t off, const p_xform_t *x, p_out_t *o, uint depth)
{
	const ccp type = p_tag_type_at (t, off);
	if (!type || depth > 16 || off + 0x60 > t->data_n || o->n_shapes > 1000000)
		return;
	if (!strcmp (type, "hknpCompoundShape"))
	{
		const s32 rel = (s32)p_le32 (t->data + off + 0x48);
		const u32 cnt = p_le32 (t->data + off + 0x50);
		const s64 arr = (s64)off + 0x48 + rel;
		if (arr < 0 || (u64)arr + 64ull * cnt > t->data_n)
			return;
		for (u32 i = 0; i < cnt; i++)
		{
			const u8 *e = t->data + arr + 64 * (size_t)i;
			const s64 rp = (s64)((u64)p_le32 (e + 48) | (u64)p_le32 (e + 52) << 32);
			const s64 target = arr + 64 * (s64)i + 48 + rp;
			if (!rp || target < 0 || (u64)target >= t->data_n)
				continue;
			const float qx = p_lef32 (e), qy = p_lef32 (e + 4), qz = p_lef32 (e + 8), qw = p_lef32 (e + 12);
			const float R[3][3] = { { 1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qz * qw), 2 * (qx * qz + qy * qw) },
				{ 2 * (qx * qy + qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qx * qw) },
				{ 2 * (qx * qz - qy * qw), 2 * (qy * qz + qx * qw), 1 - 2 * (qx * qx + qy * qy) } };
			const float sc[3] = { p_lef32 (e + 32), p_lef32 (e + 36), p_lef32 (e + 40) };
			p_xform_t y;
			for (int r = 0; r < 3; r++)
			{
				for (int c = 0; c < 3; c++)
					y.m[r][c] = x->m[r][0] * R[0][c] * sc[c] + x->m[r][1] * R[1][c] * sc[c] + x->m[r][2] * R[2][c] * sc[c];
				y.t[r] = x->m[r][0] * p_lef32 (e + 16) + x->m[r][1] * p_lef32 (e + 20) + x->m[r][2] * p_lef32 (e + 24) + x->t[r];
			}
			p_compound_shape (t, (size_t)target, &y, o, depth + 1);
		}
		return;
	}
	o->n_shapes++;
	sb_printf (o->b, "o %u_%s\n", o->n_shapes, strncmp (type, "hknp", 4) ? type : type + 4);
	if (!strcmp (type, "hknpMeshShape"))
		p_compound_mesh (t, off, x, o);
	else if (!strcmp (type, "hknpCapsuleShape"))
	{
		if (!p_compound_capsule (t, off, x, o))
			o->n_other++;
	}
	else if (!p_compound_convex (t, off, x, o))
	{
		sb_printf (o->b, "# %s is not converted\n", type);
		o->n_other++;
	}
}

bool IsPhiveCompound (const u8 *data, size_t size)
{
	return IsPhiveFile (data, size) && data[6] == 2;
}

enumError ConvertPhiveCompoundToObj (const u8 *data, size_t size, char **obj_out, size_t *obj_size)
{
	*obj_out = 0;
	*obj_size = 0;
	if (!IsPhiveCompound (data, size))
		return ERR_WRONG_FILE_TYPE;

	size_t tags[16];
	uint n_tags = 0;
	for (size_t p = 0x0c; p + 4 <= 0x0c + 4 * 20 && p + 4 <= size; p += 4)
	{
		const size_t o = p_le32 (data + p);
		if (!o || o + 16 > size || memcmp (data + o + 4, "TAG0", 4))
			continue;
		bool dup = false;
		for (uint i = 0; i < n_tags; i++)
			dup |= tags[i] == o;
		if (!dup && n_tags < 16)
			tags[n_tags++] = o;
	}
	if (!n_tags)
		return ERR_NOTHING_TO_DO; // an empty body (most of them) or a water grid

	sbuf_t b = { 0 };
	p_out_t o = { &b, 0, 0, 0, 0, 0, 0 };
	sb_printf (&b, "# Phive static compound body: %u Havok compound shape%s\n", n_tags, n_tags == 1 ? "" : "s");
	for (uint i = 0; i < n_tags; i++)
	{
		p_tag_t t;
		if (!p_tag_parse (data + tags[i], size - tags[i], &t))
			continue;
		const p_xform_t id = { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, { 0, 0, 0 } };
		for (uint n = 0; n < t.n_items; n++)
			if (t.items[n].type)
			{
				sb_printf (&b, "g compound_%u\n", i);
				p_compound_shape (&t, t.items[n].off, &id, &o, 0);
				break;
			}
		p_tag_free (&t);
	}
	sb_printf (&b, "# %u shapes: %u meshes, %u convex, %u capsules, %u not converted\n", o.n_shapes, o.n_mesh, o.n_convex,
		o.n_capsule, o.n_other);
	if (b.fail || !o.n_shapes)
	{
		FREE (b.d);
		return b.fail ? ERR_OUT_OF_MEMORY : ERR_NOTHING_TO_DO;
	}
	*obj_out = b.d;
	*obj_size = b.n;
	return ERR_OK;
}

// ----------------------------------------------------------------------------------------------------------------
// Water grids in static compound bodies (Mario Kart World "*Water*" bodies): section 1 of the offset table holds
// {f32 size x/y/z, f32 center x/y/z, u32 width, u32 height, u32 grid bytes (w * h * 8), u32 0, u32 w * h * 2}, and
// section 8 the grid: per cell {u16 height (0..65535 over the box height), u16 flow x, u16 flow z (0x7fff = 0),
// u16 flags}, rows of 'width' cells.

enumError DecodePhiveWaterGrid (const u8 *data, size_t size, u16 **cells, uint *width, uint *height, float box[6])
{
	*cells = 0;
	if (!IsPhiveCompound (data, size) || size < 0x0c + 4 * 9)
		return ERR_NOTHING_TO_DO;
	const size_t hdr = p_le32 (data + 0x0c + 4), grid = p_le32 (data + 0x0c + 4 * 8);
	if (hdr + 0x2c > size)
		return ERR_NOTHING_TO_DO;
	const u32 w = p_le32 (data + hdr + 0x18), h = p_le32 (data + hdr + 0x1c), bytes = p_le32 (data + hdr + 0x20);
	if (!w || !h || w > 4096 || h > 4096 || bytes != w * h * 8 || grid + bytes > size)
		return ERR_NOTHING_TO_DO;
	u16 *c = MALLOC ((size_t)w * h * 4 * sizeof (u16));
	if (!c)
		return ERR_OUT_OF_MEMORY;
	for (size_t i = 0; i < (size_t)w * h * 4; i++)
		c[i] = p_le16 (data + grid + 2 * i);
	for (int i = 0; i < 6; i++)
		box[i] = p_lef32 (data + hdr + 4 * i);
	*cells = c;
	*width = w;
	*height = h;
	return ERR_OK;
}
