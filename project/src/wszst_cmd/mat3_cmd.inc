// J3D MAT3 material viewer/editor, ported from GCFT (gclib/j3d_chunks/mat3.py).
//
//   wszst MAT3 LIST file.(bmd|bdl|bmt)
//   wszst MAT3 SET  in out key=value... [mat=N|name]... [force]
//
// Edits are made in place inside the shared MAT3 lists (the section is never resized). If a value is shared
// with a material that was not selected, SET refuses unless "force" is given (then all sharers change).
// Without mat=, every material is selected. Colors are RRGGBBAA hex. Fields that are unset (index 0xFF/0xFFFF)
// cannot be edited. TEV stages, tex matrices, fog, indirect and the rest of GCFT's editor are not covered.

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

typedef struct m3f_t
{
	ccp key;
	uint rec; // offset of the index (or inline value) inside the 0x14c byte material record
	uint iw; // index width in bytes (1/2), 0 = value stored inline in the record
	uint list; // MAT3 list number (index into the 30 section offsets)
	uint esz; // element size inside the list
	uint eoff; // byte offset of the value inside the element
	uint w; // value width in bytes (1, 4)
	const m3e_t *en; // enum names, NULL = plain number / hex color
	bool color;
} m3f_t;

#define M3_REC 0x14c
static const m3f_t *m3_table (uint *n)
{
	static m3f_t t[40];
	static uint cnt;
	if (!cnt)
	{
#define F(k, rec, iw, list, esz, eoff, w, en, col) t[cnt++] = (m3f_t){ k, rec, iw, list, esz, eoff, w, en, col }
		F ("pixel_mode", 0, 0, 0, 0, 0, 1, m3_pmode, 0);
		F ("cull", 1, 1, 4, 4, 0, 4, m3_cull, 0);
		F ("z_compare", 5, 1, 27, 1, 0, 1, m3_bool, 0);
		F ("depth_test", 6, 1, 26, 4, 0, 1, m3_bool, 0);
		F ("depth_func", 6, 1, 26, 4, 1, 1, m3_cmp, 0);
		F ("depth_write", 6, 1, 26, 4, 2, 1, m3_bool, 0);
		F ("dither", 7, 1, 28, 1, 0, 1, m3_bool, 0);
		F ("alpha_comp0", 0x146, 2, 24, 8, 0, 1, m3_cmp, 0);
		F ("alpha_ref0", 0x146, 2, 24, 8, 1, 1, 0, 0);
		F ("alpha_op", 0x146, 2, 24, 8, 2, 1, m3_aop, 0);
		F ("alpha_comp1", 0x146, 2, 24, 8, 3, 1, m3_cmp, 0);
		F ("alpha_ref1", 0x146, 2, 24, 8, 4, 1, 0, 0);
		F ("blend_mode", 0x148, 2, 25, 4, 0, 1, m3_bmode, 0);
		F ("blend_src", 0x148, 2, 25, 4, 1, 1, m3_bfac, 0);
		F ("blend_dst", 0x148, 2, 25, 4, 2, 1, m3_bfac, 0);
		F ("blend_logic", 0x148, 2, 25, 4, 3, 1, m3_lop, 0);
		F ("mat_color0", 8, 2, 5, 4, 0, 4, 0, 1);
		F ("mat_color1", 10, 2, 5, 4, 0, 4, 0, 1);
		F ("ambient0", 0x14, 2, 8, 4, 0, 4, 0, 1);
		F ("ambient1", 0x16, 2, 8, 4, 0, 4, 0, 1);
		for (uint i = 0; i < 4; i++)
		{
			static char kn[4][8];
			snprintf (kn[i], sizeof (kn[i]), "konst%u", i);
			F (kn[i], 0x94 + 2 * i, 2, 18, 4, 0, 4, 0, 1);
		}
#undef F
	}
	*n = cnt;
	return t;
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

// Index stored in a record slot, -1 if unset / inline.
static int m3_index (const m3_t *m, uint r, const m3f_t *f)
{
	if (!f->iw)
		return -1;
	const u8 *p = m3_rec (m, r) + f->rec;
	const uint v = f->iw == 1 ? p[0] : m3_r16 (p);
	return v == (f->iw == 1 ? 0xffu : 0xffffu) ? -1 : (int)v;
}

// Pointer to the value bytes, or NULL if unset / out of range.
static u8 *m3_value (const m3_t *m, uint r, const m3f_t *f)
{
	if (!f->iw)
		return m3_rec (m, r) + f->rec;
	const int ix = m3_index (m, r, f);
	if (ix < 0 || !m->offs[f->list])
		return 0;
	const size_t off = m->sect + m->offs[f->list] + (size_t)ix * f->esz + f->eoff;
	return off + f->w <= m->sz ? m->d + off : 0;
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

static void m3_print_value (const m3f_t *f, const u8 *p)
{
	u32 v = 0;
	for (uint i = 0; i < f->w; i++)
		v = v << 8 | p[i];
	if (f->color)
		printf ("%08X", v);
	else if (f->en)
	{
		for (const m3e_t *e = f->en; e->name; e++)
			if ((u32)e->val == v)
				return (void)fputs (e->name, stdout);
		printf ("%u", v);
	}
	else
		printf ("%u", v);
}

static int m3_parse (const m3f_t *f, ccp s, u32 *out)
{
	if (f->en)
		for (const m3e_t *e = f->en; e->name; e++)
			if (!strcasecmp (s, e->name))
				return *out = (u32)e->val, 0;
	char *end;
	const unsigned long long v = strtoull (s, &end, f->color ? 16 : 0);
	if (!*s || *end || v > (f->w == 4 ? 0xffffffffull : 0xffull))
		return 1;
	if (f->en && !f->color && f->w == 1)
	{
		bool ok = false;
		for (const m3e_t *e = f->en; e->name; e++)
			ok |= (u32)e->val == v;
		if (!ok)
			return 1;
	}
	return *out = (u32)v, 0;
}

static enumError cmd_mat3 ()
{
	ParamList_t *pa = first_param;
	if (!pa || !pa->next)
		return ERROR0 (ERR_MISSING_PARAM, "Usage: wszst MAT3 LIST file | MAT3 SET in out key=value... [mat=N|name] [force]\n");
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
	uint nf;
	const m3f_t *tab = m3_table (&nf);
	enumError err = ERR_OK;
	char nb[0x40];

	if (!set)
	{
		for (uint i = 0; i < m->nmat; i++)
		{
			printf ("\n[%u] %s (record %u)\n", i, m3_matname (m, i, nb, sizeof (nb)), m->remap[i]);
			for (uint k = 0; k < nf; k++)
			{
				const u8 *p = m3_value (m, m->remap[i], &tab[k]);
				printf ("  %-12s ", tab[k].key);
				if (p)
					m3_print_value (&tab[k], p);
				else
					fputs ("(unset)", stdout);
				putchar ('\n');
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
		// selected records
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
			const m3f_t *f = 0;
			for (uint k = 0; k < nf; k++)
				if (strlen (tab[k].key) == (size_t)(eq - q->arg) && !strncasecmp (tab[k].key, q->arg, eq - q->arg))
					f = &tab[k];
			if (!f)
			{
				err = ERROR0 (ERR_SYNTAX, "Unknown MAT3 key: %.*s\n", (int)(eq - q->arg), q->arg);
				break;
			}
			u32 v;
			if (m3_parse (f, eq + 1, &v))
			{
				err = ERROR0 (ERR_SEMANTIC, "Invalid value: %s\n", q->arg);
				break;
			}
			// Pass 1: check, pass 2: write.
			for (int pass = 0; pass < 2 && !err; pass++)
				for (uint r = 0; r < m->nrec && !err; r++)
				{
					if (!rsel[r])
						continue;
					u8 *p = m3_value (m, r, f);
					if (!p)
					{
						err = ERROR0 (ERR_SEMANTIC, "Field %s is unset in a selected material\n", f->key);
						break;
					}
					if (!pass)
					{
						bool shared = !f->iw && rsh[r];
						const int ix = m3_index (m, r, f);
						for (uint o = 0; o < m->nrec && f->iw && !shared; o++)
							for (uint k = 0; k < nf && !shared; k++)
								if (tab[k].iw && tab[k].list == f->list && m3_index (m, o, &tab[k]) == ix
									&& !(rsel[o] && tab[k].rec == f->rec))
									shared = true;
						if (shared && !force)
							err = ERROR0 (ERR_SEMANTIC,
								"%s is shared with unselected materials/fields; use \"force\" to change all\n", f->key);
					}
					else
					{
						for (uint i = 0; i < f->w; i++)
							p[i] = (u8)(v >> (8 * (f->w - 1 - i)));
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
