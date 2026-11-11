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
