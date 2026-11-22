// J3D MAT3 material viewer/editor, ported from GCFT (gclib/j3d_chunks/mat3.py).
//
//   wszst MAT3 LIST file.(bmd|bdl|bmt) [key-prefix|all]...
//   wszst MAT3 SET  in out key=value... [mat=N|name]... [force]
//
// Edits are made in place inside the shared MAT3 lists (the section is never resized), so a material can only
// use values that the file's lists already contain; to change a value that is shared with a material that was
// not selected, give "force" (all sharers then change). Without mat=, every material is selected.
// Keys are table driven (see m3_groups): e.g. cull, depth_func, blend_dst, mat_color0 (RRGGBBAA hex), tex0,
// tev3_color_a, order0_texmap, tevcolor1_r, swap2_ras, swaptab0_g, texgen0_source, texmtx0_scale_x,
// texmtx0_rotation (degrees), chan0_lighting, fog_start, nbt_scale_x, indstage0_format ...
// LIST without arguments prints the basic keys; "all" prints every key that is set, a prefix filters.

enum { MK_INT, MK_FLOAT, MK_SINT, MK_ROT, MK_COLOR };
enum { MM_REC, MM_INLINE, MM_INDIRECT }; // indexed via list / stored in the record / per-material block

typedef struct m3e_t { ccp name; int val; } m3e_t;
#define M3E(...) (const m3e_t[]){ __VA_ARGS__, { 0, 0 } }

static const m3e_t *const m3_cull = M3E ({ "none", 0 }, { "front", 1 }, { "back", 2 }, { "all", 3 });
static const m3e_t *const m3_cmp = M3E ({ "never", 0 }, { "less", 1 }, { "equal", 2 }, { "less_equal", 3 },
	{ "greater", 4 }, { "not_equal", 5 }, { "greater_equal", 6 }, { "always", 7 });
static const m3e_t *const m3_aop = M3E ({ "and", 0 }, { "or", 1 }, { "xor", 2 }, { "xnor", 3 });
static const m3e_t *const m3_bmode = M3E ({ "none", 0 }, { "blend", 1 }, { "logic", 2 }, { "subtract", 3 });
static const m3e_t *const m3_bfac = M3E ({ "zero", 0 }, { "one", 1 }, { "src_color", 2 }, { "inv_src_color", 3 },
	{ "src_alpha", 4 }, { "inv_src_alpha", 5 }, { "dst_alpha", 6 }, { "inv_dst_alpha", 7 });
static const m3e_t *const m3_lop = M3E ({ "clear", 0 }, { "and", 1 }, { "rev_and", 2 }, { "copy", 3 },
	{ "inv_and", 4 }, { "noop", 5 }, { "xor", 6 }, { "or", 7 }, { "nor", 8 }, { "equiv", 9 }, { "inv", 10 },
	{ "rev_or", 11 }, { "inv_copy", 12 }, { "inv_or", 13 }, { "nand", 14 }, { "set", 15 });
static const m3e_t *const m3_pmode = M3E ({ "opaque", 1 }, { "alpha_test", 2 }, { "translucent", 4 });
static const m3e_t *const m3_bool = M3E ({ "false", 0 }, { "true", 1 });

typedef struct m3fld_t
{
	ccp name;
	u8 eoff, w, kind, shift, nbits; // offset in the element, bytes, MK_*, bit range (nbits 0 = whole value)
	const m3e_t *en;
} m3fld_t;

typedef struct m3grp_t
{
	ccp prefix;
	uint count; // 0: single instance
	uint recbase, recstep;
	u8 mode, iw, list, esz; // MM_*, index width (1/2), MAT3 list number, element size
	bool basic; // shown by LIST without arguments
	const m3fld_t *f; // terminated by name == NULL
} m3grp_t;

typedef struct m3f_t // a resolved field
{
	const m3grp_t *g;
	uint rec; // record offset of the index/value (MM_REC/MM_INLINE) or element base (MM_INDIRECT)
	m3fld_t f;
} m3f_t;

#define U8F(n, o) { n, o, 1, MK_INT, 0, 0, 0 }
#define FLF(n, o) { n, o, 4, MK_FLOAT, 0, 0, 0 }
#define COLF(n, o) { n, o, 4, MK_COLOR, 0, 0, 0 }

static const m3fld_t m3_f_tev[] = { U8F ("mode", 0), U8F ("color_a", 1), U8F ("color_b", 2), U8F ("color_c", 3),
	U8F ("color_d", 4), U8F ("color_op", 5), U8F ("color_bias", 6), U8F ("color_scale", 7), U8F ("color_clamp", 8),
	U8F ("color_reg", 9), U8F ("alpha_a", 10), U8F ("alpha_b", 11), U8F ("alpha_c", 12), U8F ("alpha_d", 13),
	U8F ("alpha_op", 14), U8F ("alpha_bias", 15), U8F ("alpha_scale", 16), U8F ("alpha_clamp", 17),
	U8F ("alpha_reg", 18), { 0 } };
static const m3fld_t m3_f_texmtx[] = { { "projection", 0, 1, MK_INT, 0, 0, 0 },
	{ "map_mode", 1, 1, MK_INT, 0, 6, 0 }, { "is_maya", 1, 1, MK_INT, 7, 1, 0 },
	FLF ("center_x", 4), FLF ("center_y", 8), FLF ("center_z", 12), FLF ("scale_x", 0x10), FLF ("scale_y", 0x14),
	{ "rotation", 0x18, 2, MK_ROT, 0, 0, 0 }, FLF ("translation_x", 0x1c), FLF ("translation_y", 0x20),
	FLF ("m00", 0x24), FLF ("m01", 0x28), FLF ("m02", 0x2c), FLF ("m03", 0x30), FLF ("m10", 0x34), FLF ("m11", 0x38),
	FLF ("m12", 0x3c), FLF ("m13", 0x40), FLF ("m20", 0x44), FLF ("m21", 0x48), FLF ("m22", 0x4c), FLF ("m23", 0x50),
	FLF ("m30", 0x54), FLF ("m31", 0x58), FLF ("m32", 0x5c), FLF ("m33", 0x60), { 0 } };
static const m3fld_t m3_f_indmtx[] = { FLF ("m00", 0), FLF ("m01", 4), FLF ("m02", 8), FLF ("m10", 12),
	FLF ("m11", 16), FLF ("m12", 20), { "exp", 24, 1, MK_SINT, 0, 0, 0 }, { 0 } };
static const m3fld_t m3_f_indstage[] = { U8F ("tev_stage", 0), U8F ("format", 1), U8F ("bias_sel", 2),
	U8F ("mtx_sel", 3), U8F ("wrap_s", 4), U8F ("wrap_t", 5), U8F ("add_prev", 6), U8F ("utc_lod", 7),
	U8F ("alpha_sel", 8), { 0 } };
static const m3fld_t m3_f_fog[] = { U8F ("type", 0), U8F ("enable", 1), { "center", 2, 2, MK_INT, 0, 0, 0 },
	FLF ("start", 4), FLF ("end", 8), FLF ("near", 12), FLF ("far", 0x10), COLF ("color", 0x14), { 0 } };

// Compound literals inside m3_groups die with its block, so field lists are copied into static storage.
static const m3fld_t *m3_dup (const m3fld_t *src, size_t bytes)
{
	static m3fld_t pool[256];
	static uint used;
	const uint n = (uint)(bytes / sizeof (*src));
	m3fld_t *r = pool + used;
	if (used + n > 256)
		return 0;
	memcpy (r, src, bytes);
	used += n;
	return r;
}

static const m3grp_t *m3_groups (uint *n)
{
	static m3grp_t g[64];
	static uint c;
	if (!c)
	{
#define G(pre, cnt, rb, rs, mode, iw, list, esz, bas, flds) \
	g[c++] = (m3grp_t){ pre, cnt, rb, rs, mode, iw, list, esz, bas, flds }
#define F(...) m3_dup ((const m3fld_t[]){ __VA_ARGS__, { 0 } }, sizeof ((const m3fld_t[]){ __VA_ARGS__, { 0 } }))
		G ("", 0, 0, 0, MM_INLINE, 0, 0, 0, 1, F ({ "pixel_mode", 0, 1, MK_INT, 0, 0, m3_pmode }));
		G ("", 0, 1, 0, MM_REC, 1, 4, 4, 1, F ({ "cull", 0, 4, MK_INT, 0, 0, m3_cull }));
		G ("", 0, 5, 0, MM_REC, 1, 27, 1, 1, F ({ "z_compare", 0, 1, MK_INT, 0, 0, m3_bool }));
		G ("", 0, 6, 0, MM_REC, 1, 26, 4, 1, F ({ "depth_test", 0, 1, MK_INT, 0, 0, m3_bool },
			{ "depth_func", 1, 1, MK_INT, 0, 0, m3_cmp }, { "depth_write", 2, 1, MK_INT, 0, 0, m3_bool }));
		G ("", 0, 7, 0, MM_REC, 1, 28, 1, 1, F ({ "dither", 0, 1, MK_INT, 0, 0, m3_bool }));
		G ("", 0, 0x146, 0, MM_REC, 2, 24, 8, 1, F ({ "alpha_comp0", 0, 1, MK_INT, 0, 0, m3_cmp },
			U8F ("alpha_ref0", 1), { "alpha_op", 2, 1, MK_INT, 0, 0, m3_aop },
			{ "alpha_comp1", 3, 1, MK_INT, 0, 0, m3_cmp }, U8F ("alpha_ref1", 4)));
		G ("", 0, 0x148, 0, MM_REC, 2, 25, 4, 1, F ({ "blend_mode", 0, 1, MK_INT, 0, 0, m3_bmode },
			{ "blend_src", 1, 1, MK_INT, 0, 0, m3_bfac }, { "blend_dst", 2, 1, MK_INT, 0, 0, m3_bfac },
			{ "blend_logic", 3, 1, MK_INT, 0, 0, m3_lop }));
		G ("mat_color", 2, 8, 2, MM_REC, 2, 5, 4, 1, F (COLF ("", 0)));
		G ("ambient", 2, 0x14, 2, MM_REC, 2, 8, 4, 1, F (COLF ("", 0)));
		G ("konst", 4, 0x94, 2, MM_REC, 2, 18, 4, 1, F (COLF ("", 0)));
		G ("", 0, 2, 0, MM_REC, 1, 6, 1, 1, F (U8F ("num_color_chans", 0)));
		G ("", 0, 3, 0, MM_REC, 1, 10, 1, 1, F (U8F ("num_tex_gens", 0)));
		G ("", 0, 4, 0, MM_REC, 1, 19, 1, 1, F (U8F ("num_tev_stages", 0)));
		G ("tex", 8, 0x84, 2, MM_REC, 2, 15, 2, 1, F ({ "", 0, 2, MK_INT, 0, 0, 0 }));
		G ("tev", 16, 0xe4, 2, MM_REC, 2, 20, 0x14, 0, m3_f_tev);
		G ("order", 16, 0xbc, 2, MM_REC, 2, 16, 4, 0, F (U8F ("texcoord", 0), U8F ("texmap", 1), U8F ("channel", 2)));
		G ("tevcolor", 4, 0xdc, 2, MM_REC, 2, 17, 8, 0, F ({ "r", 0, 2, MK_SINT, 0, 0, 0 },
			{ "g", 2, 2, MK_SINT, 0, 0, 0 }, { "b", 4, 2, MK_SINT, 0, 0, 0 }, { "a", 6, 2, MK_SINT, 0, 0, 0 }));
		G ("swap", 16, 0x104, 2, MM_REC, 2, 21, 4, 0, F (U8F ("ras", 0), U8F ("tex", 1)));
		G ("swaptab", 16, 0x124, 2, MM_REC, 2, 22, 4, 0, F (U8F ("r", 0), U8F ("g", 1), U8F ("b", 2), U8F ("a", 3)));
		G ("konstsel", 16, 0x9c, 1, MM_INLINE, 0, 0, 0, 0, F ({ "", 0, 1, MK_INT, 0, 0, 0 }));
		G ("konstasel", 16, 0xac, 1, MM_INLINE, 0, 0, 0, 0, F ({ "", 0, 1, MK_INT, 0, 0, 0 }));
		G ("texgen", 8, 0x28, 2, MM_REC, 2, 11, 4, 0, F (U8F ("type", 0), U8F ("source", 1), U8F ("matrix", 2)));
		G ("posttexgen", 8, 0x38, 2, MM_REC, 2, 12, 4, 0, F (U8F ("type", 0), U8F ("source", 1), U8F ("matrix", 2)));
		G ("texmtx", 10, 0x48, 2, MM_REC, 2, 13, 0x64, 0, m3_f_texmtx);
		G ("posttexmtx", 20, 0x5c, 2, MM_REC, 2, 14, 0x64, 0, m3_f_texmtx);
		G ("chan", 4, 0xc, 2, MM_REC, 2, 7, 8, 0, F (U8F ("lighting", 0), U8F ("mat_src", 1), U8F ("lit_mask", 2),
			U8F ("diffuse", 3), U8F ("atten", 4), U8F ("ambient_src", 5)));
		G ("light", 8, 0x18, 2, MM_REC, 2, 9, 4, 0, F (COLF ("", 0)));
		G ("fog", 0, 0x144, 0, MM_REC, 2, 23, 0x2c, 0, m3_f_fog);
		G ("fogrange", 10, 0x144, 0, MM_REC, 2, 23, 0x2c, 0, F ({ "", 0, 2, MK_INT, 0, 0, 0 })); // eoff set in m3_resolve
		G ("nbt", 0, 0x14a, 0, MM_REC, 2, 29, 0x10, 0, F (U8F ("enable", 0), FLF ("scale_x", 4), FLF ("scale_y", 8),
			FLF ("scale_z", 12)));
		G ("ind", 0, 0, 0, MM_INDIRECT, 0, 3, 0, 0, F (U8F ("enable", 0), U8F ("stages", 1)));
		G ("indorder", 4, 4, 4, MM_INDIRECT, 0, 3, 0, 0, F (U8F ("texcoord", 0), U8F ("texmap", 1)));
		G ("indmtx", 3, 0x14, 28, MM_INDIRECT, 0, 3, 0, 0, m3_f_indmtx);
		G ("indscale", 4, 0x68, 4, MM_INDIRECT, 0, 3, 0, 0, F (U8F ("s", 0), U8F ("t", 1)));
		G ("indstage", 16, 0x78, 12, MM_INDIRECT, 0, 3, 0, 0, m3_f_indstage);
#undef F
#undef G
	}
	*n = c;
	return g;
}

#define M3_REC 0x14c
#define M3_IND 0x138

// Key text of field f of instance n in group g.
static void m3_key (const m3grp_t *g, uint n, const m3fld_t *f, char *buf, size_t bl)
{
	if (g->count)
		snprintf (buf, bl, "%s%u%s%s", g->prefix, n, f->name[0] ? "_" : "", f->name);
	else
		snprintf (buf, bl, "%s%s%s", g->prefix, g->prefix[0] && f->name[0] ? "_" : "", f->name);
}

static void m3_fill (const m3grp_t *g, uint n, const m3fld_t *f, m3f_t *out)
{
	out->g = g;
	out->rec = g->recbase + n * g->recstep;
	out->f = *f;
	if (!strcmp (g->prefix, "fogrange"))
		out->f.eoff = (u8)(0x18 + 2 * n);
}

static int m3_resolve (ccp key, m3f_t *out)
{
	uint ng;
	const m3grp_t *g = m3_groups (&ng);
	char k[48];
	for (uint i = 0; i < ng; i++)
		for (uint n = 0; n < (g[i].count ? g[i].count : 1); n++)
			for (const m3fld_t *f = g[i].f; f->name; f++)
			{
				m3_key (&g[i], n, f, k, sizeof (k));
				if (!strcmp (k, key))
					return m3_fill (&g[i], n, f, out), 0;
			}
	return 1;
}

typedef struct m3_t
{
	u8 *d;
	size_t sz;
	size_t sect; // MAT3 section offset
	uint nmat, nrec;
	u32 offs[30];
	uint remap[1]; // flexible: nmat entries (allocated bigger)
} m3_t;

static u32 m3_r32 (const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u32 m3_r16 (const u8 *p) { return (u32)(p[0] << 8 | p[1]); }

static m3_t *m3_open (u8 *d, size_t sz)
{
	if (sz < 0x20 || memcmp (d, "J3D2", 4))
		return 0;
	for (size_t pos = 0x20; pos + 8 <= sz;)
	{
		const u32 ss = m3_r32 (d + pos + 4);
		if (ss < 8 || pos + ss > sz)
			return 0;
		if (!memcmp (d + pos, "MAT3", 4))
		{
			if (ss < 0x84)
				return 0;
			const uint n = m3_r16 (d + pos + 8);
			m3_t *m = CALLOC (1, sizeof (*m) + n * sizeof (uint));
			m->d = d, m->sz = sz, m->sect = pos, m->nmat = n;
			for (uint i = 0; i < 30; i++)
			{
				m->offs[i] = m3_r32 (d + pos + 0xc + i * 4);
				if (m->offs[i] > ss)
					goto bad;
			}
			m->nrec = m->offs[1] > m->offs[0] ? (m->offs[1] - m->offs[0]) / M3_REC : 0;
			if (!m->offs[0] || !m->offs[1] || m->offs[1] + 2 * n > ss)
				goto bad;
			for (uint i = 0; i < n; i++)
			{
				m->remap[i] = m3_r16 (d + pos + m->offs[1] + 2 * i);
				if (m->remap[i] >= m->nrec)
					goto bad;
			}
			return m;
		bad:
			FREE (m);
			return 0;
		}
		pos += ss;
	}
	return 0;
}

static u8 *m3_rec (const m3_t *m, uint r) { return m->d + m->sect + m->offs[0] + (size_t)r * M3_REC; }

// Index stored in a record slot, -1 if unset / not indexed.
static int m3_index (const m3_t *m, uint r, const m3f_t *f)
{
	if (f->g->mode != MM_REC)
		return -1;
	const u8 *p = m3_rec (m, r) + f->rec;
	const uint v = f->g->iw == 1 ? p[0] : m3_r16 (p);
	return v == (f->g->iw == 1 ? 0xffu : 0xffffu) ? -1 : (int)v;
}

// Pointer to the value bytes of record/material #u (record for MM_REC/MM_INLINE, material for MM_INDIRECT).
static u8 *m3_value (const m3_t *m, uint u, const m3f_t *f)
{
	size_t off;
	if (f->g->mode == MM_INLINE)
		off = m->sect + m->offs[0] + (size_t)u * M3_REC + f->rec + f->f.eoff;
	else if (f->g->mode == MM_INDIRECT)
	{
		if (!m->offs[3] || m->offs[3] == m->offs[2])
			return 0;
		off = m->sect + m->offs[3] + (size_t)u * M3_IND + f->rec + f->f.eoff;
	}
	else
	{
		const int ix = m3_index (m, u, f);
		if (ix < 0 || !m->offs[f->g->list])
			return 0;
		off = m->sect + m->offs[f->g->list] + (size_t)ix * f->g->esz + f->f.eoff;
	}
	return off + f->f.w <= m->sz ? m->d + off : 0;
}

static ccp m3_matname (const m3_t *m, uint i, char *buf, size_t bl)
{
	snprintf (buf, bl, "mat%u", i);
	const u32 no = m->offs[2];
	if (no && m->sect + no + 4 + (size_t)m->nmat * 4 <= m->sz && m3_r16 (m->d + m->sect + no) == m->nmat)
	{
		const size_t s = m->sect + no + m3_r16 (m->d + m->sect + no + 4 + i * 4 + 2);
		if (s < m->sz)
			snprintf (buf, bl, "%.*s", (int)(m->sz - s < bl - 1 ? m->sz - s : bl - 1), m->d + s);
	}
	return buf;
}

static u32 m3_raw (const u8 *p, uint w)
{
	u32 v = 0;
	for (uint i = 0; i < w; i++)
		v = v << 8 | p[i];
	return v;
}

static void m3_print_value (const m3fld_t *f, const u8 *p)
{
	u32 v = m3_raw (p, f->w);
	if (f->kind == MK_COLOR)
		printf ("%08X", v);
	else if (f->kind == MK_FLOAT)
	{
		float x;
		memcpy (&x, &v, 4);
		printf ("%g", x);
	}
	else if (f->kind == MK_ROT)
		printf ("%g", (double)v * 360.0 / 65536.0);
	else if (f->kind == MK_SINT)
		printf ("%d", f->w == 1 ? (int)(s8)v : f->w == 2 ? (int)(s16)v : (int)v);
	else
	{
		if (f->nbits)
			v = (v >> f->shift) & ((1u << f->nbits) - 1);
		for (const m3e_t *e = f->en; e && e->name; e++)
			if ((u32)e->val == v)
				return (void)fputs (e->name, stdout);
		printf ("%u", v);
	}
}

// Parse and store into p (w bytes). Nonzero on a bad value.
static int m3_store (const m3fld_t *f, u8 *p, ccp s)
{
	u32 raw;
	char *end;
	if (f->kind == MK_FLOAT)
	{
		const float x = strtof (s, &end);
		if (!*s || *end)
			return 1;
		memcpy (&raw, &x, 4);
	}
	else if (f->kind == MK_ROT)
	{
		const double x = strtod (s, &end);
		if (!*s || *end)
			return 1;
		raw = (u32)(long long)(x * 65536.0 / 360.0 + (x < 0 ? -0.5 : 0.5)) & 0xffff;
	}
	else if (f->kind == MK_COLOR)
	{
		const unsigned long long x = strtoull (s, &end, 16);
		if (!*s || *end || x > 0xffffffffull)
			return 1;
		raw = (u32)x;
	}
	else
	{
		long long x = -1;
		bool named = false;
		for (const m3e_t *e = f->en; e && e->name; e++)
			if (!strcasecmp (s, e->name))
				x = e->val, named = true;
		if (!named)
		{
			x = strtoll (s, &end, 0);
			if (!*s || *end)
				return 1;
			if (f->en)
			{
				bool ok = false;
				for (const m3e_t *e = f->en; e->name; e++)
					ok |= e->val == x;
				if (!ok)
					return 1;
			}
		}
		const uint nb = f->nbits ? f->nbits : f->w * 8;
		if (f->kind == MK_SINT ? (x < -(1ll << (nb - 1)) || x >= (1ll << (nb - 1))) : (x < 0 || x >= (1ll << nb)))
			return 1;
		const u32 mask = nb >= 32 ? 0xffffffffu : (1u << nb) - 1;
		if (f->nbits)
			raw = (m3_raw (p, f->w) & ~(mask << f->shift)) | (((u32)x & mask) << f->shift);
		else
			raw = (u32)x & (f->w == 4 ? 0xffffffffu : (1u << (f->w * 8)) - 1);
	}
	for (uint i = 0; i < f->w; i++)
		p[i] = (u8)(raw >> (8 * (f->w - 1 - i)));
	return 0;
}

static enumError cmd_mat3 ()
{
	ParamList_t *pa = first_param;
	if (!pa || !pa->next)
		return ERROR0 (ERR_MISSING_PARAM,
			"Usage: wszst MAT3 LIST file [prefix|all]... | MAT3 SET in out key=value... [mat=N|name] [force]\n");
	const bool set = !strcasecmp (pa->arg, "SET");
	if (!set && strcasecmp (pa->arg, "LIST"))
		return ERROR0 (ERR_SYNTAX, "Unknown MAT3 action (use LIST or SET): %s\n", pa->arg);
	u8 *d = 0;
	size_t sz = 0;
	if (LoadFileAlloc (pa->next->arg, 0, 0, &d, &sz, 0, 0, 0, false) || !d)
		return ERROR1 (ERR_CANT_OPEN, "Can't read: %s\n", pa->next->arg);
	m3_t *m = m3_open (d, sz);
	if (!m)
	{
		FREE (d);
		return ERROR0 (ERR_INVALID_DATA, "No usable MAT3 section (need J3D bmd/bdl/bmt): %s\n", pa->next->arg);
	}
	uint ng;
	const m3grp_t *grp = m3_groups (&ng);
	enumError err = ERR_OK;
	char nb[0x40];

	if (!set)
	{
		ParamList_t *flt = pa->next->next;
		bool all = false;
		for (ParamList_t *q = flt; q; q = q->next)
			all |= !strcasecmp (q->arg, "all");
		for (uint i = 0; i < m->nmat; i++)
		{
			printf ("\n[%u] %s (record %u)\n", i, m3_matname (m, i, nb, sizeof (nb)), m->remap[i]);
			for (uint gi = 0; gi < ng; gi++)
				for (uint n = 0; n < (grp[gi].count ? grp[gi].count : 1); n++)
					for (const m3fld_t *f = grp[gi].f; f->name; f++)
					{
						char key[48];
						m3_key (&grp[gi], n, f, key, sizeof (key));
						bool want = all || (!flt && grp[gi].basic);
						for (ParamList_t *q = flt; q && !want; q = q->next)
							want = !strncasecmp (key, q->arg, strlen (q->arg));
						if (!want)
							continue;
						m3f_t r;
						m3_fill (&grp[gi], n, f, &r);
						const u8 *p = m3_value (m, r.g->mode == MM_INDIRECT ? i : m->remap[i], &r);
						if (p)
						{
							printf ("  %-22s ", key);
							m3_print_value (&r.f, p);
							putchar ('\n');
						}
						else if (grp[gi].basic && !all)
							printf ("  %-22s (unset)\n", key);
					}
		}
		goto done;
	}

	if (!pa->next->next)
	{
		err = ERROR0 (ERR_MISSING_PARAM, "Missing output file!\n");
		goto done;
	}
	{
		bool *sel = CALLOC (m->nmat ? m->nmat : 1, 1);
		bool any = false, force = false;
		for (ParamList_t *q = pa->next->next->next; q; q = q->next)
		{
			if (!strcasecmp (q->arg, "force"))
				force = true;
			else if (!strncasecmp (q->arg, "mat=", 4))
			{
				bool found = false;
				char *end;
				const long n = strtol (q->arg + 4, &end, 10);
				for (uint i = 0; i < m->nmat; i++)
					if ((!*end && (long)i == n) || !strcmp (m3_matname (m, i, nb, sizeof (nb)), q->arg + 4))
						sel[i] = found = true;
				any = true;
				if (!found)
					err = ERROR0 (ERR_SEMANTIC, "No such material: %s\n", q->arg + 4);
			}
		}
		if (!any)
			memset (sel, 1, m->nmat);
		bool *rsel = CALLOC (m->nrec ? m->nrec : 1, 1), *rsh = CALLOC (m->nrec ? m->nrec : 1, 1);
		for (uint i = 0; i < m->nmat; i++)
			if (sel[i])
				rsel[m->remap[i]] = true;
		for (uint i = 0; i < m->nmat; i++)
			if (!sel[i] && rsel[m->remap[i]])
				rsh[m->remap[i]] = true; // record also used by an unselected material
		uint nset = 0;
		for (ParamList_t *q = pa->next->next->next; q && !err; q = q->next)
		{
			char *eq = strchr (q->arg, '=');
			if (!eq || !strncasecmp (q->arg, "mat=", 4))
				continue;
			char key[48];
			snprintf (key, sizeof (key), "%.*s", (int)(eq - q->arg < 47 ? eq - q->arg : 47), q->arg);
			m3f_t f;
			if (m3_resolve (key, &f))
			{
				err = ERROR0 (ERR_SYNTAX, "Unknown MAT3 key: %s\n", key);
				break;
			}
			u8 probe[4] = { 0 };
			if (m3_store (&f.f, probe, eq + 1))
			{
				err = ERROR0 (ERR_SEMANTIC, "Invalid value: %s\n", q->arg);
				break;
			}
			const bool per_mat = f.g->mode == MM_INDIRECT;
			const uint nu = per_mat ? m->nmat : m->nrec;
			for (int pass = 0; pass < 2 && !err; pass++)
				for (uint u = 0; u < nu && !err; u++)
				{
					if (!(per_mat ? sel[u] : rsel[u]))
						continue;
					u8 *p = m3_value (m, u, &f);
					if (!p)
					{
						err = ERROR0 (ERR_SEMANTIC, "Field %s is unset in a selected material\n", key);
						break;
					}
					if (!pass)
					{
						bool shared = f.g->mode == MM_INLINE && rsh[u];
						const int ix = m3_index (m, u, &f);
						for (uint o = 0; o < m->nrec && f.g->mode == MM_REC && !shared; o++)
							for (uint gi = 0; gi < ng && !shared; gi++)
							{
								if (grp[gi].mode != MM_REC || grp[gi].list != f.g->list)
									continue;
								for (uint n = 0; n < (grp[gi].count ? grp[gi].count : 1) && !shared; n++)
								{
									const uint rec = grp[gi].recbase + n * grp[gi].recstep;
									m3f_t t = { .g = &grp[gi], .rec = rec };
									if (m3_index (m, o, &t) == ix && !(rsel[o] && rec == f.rec))
										shared = true;
								}
							}
						if (shared && !force)
							err = ERROR0 (ERR_SEMANTIC,
								"%s is shared with unselected materials/fields; use \"force\" to change all\n", key);
					}
					else
					{
						m3_store (&f.f, p, eq + 1);
						nset++;
					}
				}
		}
		if (!err)
		{
			FILE *fo = fopen (pa->next->next->arg, "wb");
			if (!fo || fwrite (d, 1, sz, fo) != sz || fclose (fo))
				err = ERROR1 (ERR_CANT_CREATE, "Can't write file: %s\n", pa->next->next->arg);
			else
				printf ("MAT3 SET %s: %u value write(s)\n", pa->next->next->arg, nset);
		}
		FREE (sel);
		FREE (rsel);
		FREE (rsh);
	}
done:
	FREE (m);
	FREE (d);
	return err;
}
