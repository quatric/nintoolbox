// EA Redwood Shores "VALT"/"VBIN" attribute vaults (`.vlt` + `.bin`): the gameplay database of
// SimAnimals, SimAnimals Africa, MySims Kingdom and MySims Agents (`GameData/Vaults/*.vlt`,
// `Vaults/attribdb*.vlt`). Every key is FNV-1 32 (h = 0x811c9dc5; h *= 0x01000193; h ^= c) over the
// lowercased name; names are restored from the strings of the vault's folder (`*.bin`), of the
// game's `sys/main.dol` (searched in the parent folders) and of the string table, unresolved keys
// print as hex.
// `.vlt` = {u32 1, "VALT", u32 version (2 SimAnimals, 3 MySims), u32 size, u32 class count}.
// Version 3 then has u32 type count at 0x14 and from 0x28 the type table: 24-byte {u32 key, u16
// size, u16 flags, u16 alignment, u16 member count, ...}, followed per type by the member keys
// (u32), member type indices (u16) and member counts (u16). Then one runtime pointer per class and
// the classes: a 32-byte header {u32 key, u32 0x20, u16 field count, ...} and the fields, 16 bytes
// in v2 {u32 key, u32 type, u16 size, u16 flags, u16 count, u16 alignment} and 24 bytes in v3 {u32
// key, u32 type, u16 size, u16 flags, u16 count, u16, u16, u16} where two of the trailing words
// are the type index and a count of 8-byte constraint records that follow the fields (which two
// is told apart by walking to the end of the file). Field flag 0x10 = array.
// `.bin` = {u32 1, "VBIN", u32 version (4/5), u32 collection count, ..., u32 string count at
// 0x20}, then the collections from about 0x140: a header holding {key, class, parent} (and the
// vault key in some games) followed by {u16 entry count, u16 entry capacity, u16 data size, u16
// data capacity}, 4 pad bytes in SimAnimals, `capacity` 8-byte entries {u32 field key, u32 value}
// (word order checked against the class fields) and the data. The header width, pad, word positions
// and entry order differ between the games and are detected by walking the chain of collections
// until the string table ({u32 case-sensitive FNV key, u32 offset} per string, then the strings)
// checks out. A value is inline (first `size` bytes of the BE word) when size <= 4, else {u16
// count, u16 offset into the data}; arrays store count elements there. Array elements may instead
// be separate entries keyed FNV(field key continued over "[%05d]"). v3 types with members print as
// {member=value, ...} (a member list repeats the members of member types as "x.y": those are
// skipped; members with a count > 1 print as [..]); types that are not described print as BE words.
// Written as "attrib.txt": every class with its fields ("# class"), then every collection
// ("[class/name] : parent") with its typed values.

typedef struct sv_name_t
{
	u32 h, ord;
	const char *s;
	uint n;
} sv_name_t;
typedef struct sv_type_t
{
	u32 key;
	u16 size, align, nm;
	u32 m0; // first member in sv_t.mkey / mtype
} sv_type_t;
typedef struct sv_field_t
{
	u32 key, type;
	u16 size, flags, count;
	int ti; // index into the type table, -1 if none
} sv_field_t;
typedef struct sv_elem_t
{
	u32 h;
	u16 f, i;
} sv_elem_t;
typedef struct sv_class_t
{
	u32 key;
	uint f0, nf;
	sv_elem_t *el; // lazily built element keys
	uint nel;
} sv_class_t;
typedef struct sv_str_t
{
	u32 h, off;
} sv_str_t;
typedef struct sv_t
{
	sv_name_t *names;
	uint nnames, capnames;
	sv_type_t *types;
	uint ntypes;
	u32 *mkey;
	u16 *mtype, *mcount;
	sv_field_t *fields;
	uint nfields;
	sv_class_t *cls;
	uint ncls;
	sv_str_t *strs;
	uint nstrs;
	uint maxel; // element indices tried for array fields: the largest entry count
	const u8 *pool;
	size_t poolsz;
	bcsv_buf_t out;
} sv_t;

static u32 sv_fnv (u32 h, const u8 *s, size_t n, bool lower)
{
	for (size_t i = 0; i < n; i++)
	{
		u8 c = s[i];
		if (lower && c >= 'A' && c <= 'Z')
			c += 32;
		h = h * 0x01000193u ^ c;
	}
	return h;
}

static int sv_name_cmp (const void *x, const void *y)
{
	const sv_name_t *a = x, *b = y;
	return a->h != b->h ? (a->h < b->h ? -1 : 1) : (a->ord < b->ord ? -1 : a->ord > b->ord);
}

static int sv_u32_cmp (const void *x, const void *y)
{
	const u32 a = *(const u32 *)x, b = *(const u32 *)y;
	return a < b ? -1 : a > b;
}

static void sv_add_name (sv_t *v, const u8 *p, size_t n)
{
	if (v->nnames == v->capnames)
	{
		v->capnames = v->capnames * 2 + 4096;
		v->names = REALLOC (v->names, v->capnames * sizeof (*v->names));
	}
	sv_name_t *e = v->names + v->nnames;
	e->h = sv_fnv (0x811c9dc5u, p, n, true);
	e->ord = v->nnames++;
	e->s = (const char *)p;
	e->n = (uint)n;
}

// Adds every run of 2..255 printable characters as a name candidate.
static void sv_add_strings (sv_t *v, const u8 *p, size_t n)
{
	size_t i = 0;
	while (i < n)
	{
		size_t j = i;
		while (j < n && p[j] >= 0x20 && p[j] < 0x7f)
			j++;
		if (j - i >= 2 && j - i < 256)
			sv_add_name (v, p + i, j - i);
		i = j + 1;
	}
}

static const sv_name_t *sv_lookup (const sv_t *v, u32 h)
{
	if (!v->nnames)
		return 0;
	uint lo = 0, hi = v->nnames;
	while (lo < hi)
	{
		const uint m = (lo + hi) / 2;
		if (v->names[m].h < h)
			lo = m + 1;
		else
			hi = m;
	}
	return lo < v->nnames && v->names[lo].h == h ? v->names + lo : 0;
}

static void sv_puts (sv_t *v, const char *s)
{
	bcsv_put (&v->out, s, strlen (s));
}

static void sv_put_key (sv_t *v, u32 h)
{
	const sv_name_t *e = sv_lookup (v, h);
	if (e)
		bcsv_put (&v->out, e->s, e->n);
	else
	{
		char tmp[16];
		bcsv_put (&v->out, tmp, snprintf (tmp, sizeof (tmp), "0x%08x", h));
	}
}

// Lowercased type name into BUF ("" if unknown).
static const char *sv_type_name (const sv_t *v, u32 h, char *buf, size_t bufsz)
{
	const sv_name_t *e = sv_lookup (v, h);
	size_t n = 0;
	if (e)
		for (; n < e->n && n + 1 < bufsz; n++)
			buf[n] = (char)tolower ((u8)e->s[n]);
	buf[n] = 0;
	return buf;
}

static const sv_str_t *sv_text (const sv_t *v, u32 h)
{
	return v->nstrs ? bsearch (&h, v->strs, v->nstrs, sizeof (sv_str_t), sv_u32_cmp) : 0;
}

static void sv_put_float (sv_t *v, u32 u)
{
	char tmp[32];
	float x;
	memcpy (&x, &u, 4);
	bcsv_put (&v->out, tmp, snprintf (tmp, sizeof (tmp), "%g", x));
}

// Writes one value of type TYPE (type table index TI or -1) from the SIZE bytes at P (P is
// readable for at least SIZE + 16 bytes).
static void sv_put_value (sv_t *v, u32 type, int ti, const u8 *p, uint size, uint depth)
{
	char tn[128], tmp[64];
	sv_type_name (v, type, tn, sizeof (tn));
	if (!strcmp (tn, "ea.reflection.float"))
	{
		sv_put_float (v, rd_be32 (p));
		return;
	}
	if (!strcmp (tn, "ea.reflection.double"))
	{
		const u64 u = (u64)rd_be32 (p) << 32 | rd_be32 (p + 4);
		double x;
		memcpy (&x, &u, 8);
		bcsv_put (&v->out, tmp, snprintf (tmp, sizeof (tmp), "%g", x));
		return;
	}
	if (!strcmp (tn, "ea.reflection.bool"))
	{
		sv_puts (v, p[0] ? "true" : "false");
		return;
	}
	if (!strncmp (tn, "ea.reflection.", 14))
	{
		const char *s = tn + 14;
		const bool uns = *s == 'u';
		if (uns)
			s++;
		if (!strncmp (s, "int", 3)
			&& (!strcmp (s + 3, "8") || !strcmp (s + 3, "16") || !strcmp (s + 3, "32")
				|| !strcmp (s + 3, "64")))
		{
			const uint bytes = (uint)atoi (s + 3) / 8;
			u64 u = 0;
			for (uint i = 0; i < bytes; i++)
				u = u << 8 | p[i];
			if (!uns && bytes < 8 && (u >> (bytes * 8 - 1) & 1))
				u |= ~0ull << (bytes * 8);
			const int n = uns ? snprintf (tmp, sizeof (tmp), "%llu", (unsigned long long)u)
							  : snprintf (tmp, sizeof (tmp), "%lld", (long long)u);
			bcsv_put (&v->out, tmp, n);
			return;
		}
	}
	if (!strcmp (tn, "ea.reflection.text"))
	{
		const u32 k = rd_be32 (p);
		const sv_str_t *s = sv_text (v, k);
		if (s)
		{
			const char *q = (const char *)v->pool + s->off;
			const size_t l = strnlen (q, v->poolsz - s->off);
			bcsv_put (&v->out, "\"", 1);
			for (size_t i = 0; i < l; i++)
			{
				if (q[i] == '"' || q[i] == '\\')
					bcsv_put (&v->out, "\\", 1);
				if (q[i] == '\n')
					sv_puts (v, "\\n");
				else
					bcsv_put (&v->out, q + i, 1);
			}
			bcsv_put (&v->out, "\"", 1);
		}
		else if (!k)
			sv_puts (v, "\"\"");
		else
			bcsv_put (&v->out, tmp, snprintf (tmp, sizeof (tmp), "text#%08x", k));
		return;
	}
	if (!strcmp (tn, "attrib.types.vector2") || !strcmp (tn, "attrib.types.vector3")
		|| !strcmp (tn, "attrib.types.vector4") || !strcmp (tn, "attrib.types.floatcolour"))
	{
		bcsv_put (&v->out, "(", 1);
		for (uint i = 0; i + 4 <= size; i += 4)
		{
			if (i)
				sv_puts (v, ", ");
			sv_put_float (v, rd_be32 (p + i));
		}
		bcsv_put (&v->out, ")", 1);
		return;
	}
	if (!strcmp (tn, "attrib.refspec") && size >= 8)
	{
		sv_put_key (v, rd_be32 (p));
		bcsv_put (&v->out, "/", 1);
		sv_put_key (v, rd_be32 (p + 4));
		return;
	}
	if (ti >= 0 && (uint)ti < v->ntypes && v->types[ti].nm && depth < 8)
	{
		const sv_type_t *t = v->types + ti;
		uint pos = 0;
		bcsv_put (&v->out, "{", 1);
		// a member of a type with members is followed by that type's members again ("x", "x.y"),
		// describing the same bytes: skip those
		for (uint m = 0; m < t->nm;)
		{
			const uint mt = v->mtype[t->m0 + m],
					   cnt = v->mcount[t->m0 + m] ? v->mcount[t->m0 + m] : 1;
			if (mt >= v->ntypes)
				break;
			const sv_type_t *u = v->types + mt;
			const uint al = u->align ? u->align : 1;
			pos = (pos + al - 1) / al * al;
			if (m)
				sv_puts (v, ", ");
			sv_put_key (v, v->mkey[t->m0 + m]);
			bcsv_put (&v->out, "=", 1);
			if (pos + u->size * cnt > size)
				sv_puts (v, "?");
			else
			{
				if (cnt > 1)
					bcsv_put (&v->out, "[", 1);
				for (uint k = 0; k < cnt; k++)
				{
					if (k)
						sv_puts (v, ", ");
					sv_put_value (v, u->key, (int)mt, p + pos + k * u->size, u->size, depth + 1);
				}
				if (cnt > 1)
					bcsv_put (&v->out, "]", 1);
			}
			pos += u->size * cnt;
			m += 1 + u->nm;
		}
		bcsv_put (&v->out, "}", 1);
		return;
	}
	// not described: BE u32 words (names where they resolve), trailing bytes in hex
	if (size == 4)
	{
		sv_put_key (v, rd_be32 (p));
		return;
	}
	bcsv_put (&v->out, "<", 1);
	uint i = 0;
	for (; i + 4 <= size; i += 4)
	{
		if (i)
			bcsv_put (&v->out, " ", 1);
		const sv_name_t *e = sv_lookup (v, rd_be32 (p + i));
		if (e)
			bcsv_put (&v->out, e->s, e->n);
		else
			bcsv_put (&v->out, tmp, snprintf (tmp, sizeof (tmp), "%08x", rd_be32 (p + i)));
	}
	if (i < size)
		bcsv_put (&v->out, " ", 1);
	for (; i < size; i++)
		bcsv_put (&v->out, tmp, snprintf (tmp, sizeof (tmp), "%02x", p[i]));
	bcsv_put (&v->out, ">", 1);
}

// Walks the class table from O; v3 trailing word order MODE. Returns true if it ends exactly at
// the end of the file (fields/classes are filled in when STORE).
static bool sv_walk_classes (
	sv_t *v, const u8 *vb, size_t vsz, size_t o, uint ver, u32 nc, int mode, bool store)
{
	const uint FR = ver >= 3 ? 0x18 : 0x10;
	for (u32 c = 0; c < nc; c++)
	{
		if (o + 12 > vsz || rd_be32 (vb + o + 4) != 0x20)
			return false;
		const uint nf = vb[o + 8] << 8 | vb[o + 9];
		if (store)
		{
			v->cls[c].key = rd_be32 (vb + o);
			v->cls[c].f0 = v->nfields;
			v->cls[c].nf = nf;
		}
		o += 0x20;
		size_t ncon = 0;
		for (uint i = 0; i < nf; i++, o += FR)
		{
			if (o + FR > vsz)
				return false;
			const u8 *r = vb + o;
			int ti = -1;
			if (ver >= 3)
			{
				const uint a = r[14] << 8 | r[15], b = r[16] << 8 | r[17], cq = r[18] << 8 | r[19];
				const uint t = mode ? a : b;
				ncon += mode ? b : cq;
				if (t >= v->ntypes || v->types[t].key != rd_be32 (r + 4))
					return false;
				ti = (int)t;
			}
			if (store)
			{
				sv_field_t *f = v->fields + v->nfields++;
				f->key = rd_be32 (r), f->type = rd_be32 (r + 4);
				f->size = r[8] << 8 | r[9], f->flags = r[10] << 8 | r[11];
				f->count = r[12] << 8 | r[13], f->ti = ti;
			}
		}
		o += 8 * ncon;
	}
	if (store)
		v->ncls = nc;
	return o == vsz;
}

// Follows N collections from START with header width H and pad TL; returns the end offset or 0.
static size_t sv_chain (const u8 *b, size_t bsz, size_t start, uint h, uint tl, u32 n)
{
	size_t o = start;
	for (u32 i = 0; i < n; i++)
	{
		if (o + h + 8 > bsz)
			return 0;
		const uint a = b[o + h] << 8 | b[o + h + 1], cap = b[o + h + 2] << 8 | b[o + h + 3],
				   c = b[o + h + 4] << 8 | b[o + h + 5], d = b[o + h + 6] << 8 | b[o + h + 7];
		if (a > cap || c > d)
			return 0;
		o += h + 8 + (size_t)cap * 8 + d + tl;
	}
	return o <= bsz ? o : 0;
}

static const sv_class_t *sv_class (const sv_t *v, u32 key)
{
	for (uint i = 0; i < v->ncls; i++)
		if (v->cls[i].key == key)
			return v->cls + i;
	return 0;
}

static int sv_elem_cmp (const void *x, const void *y)
{
	const sv_elem_t *a = x, *b = y;
	return a->h < b->h ? -1 : a->h > b->h;
}

// Element key -> (field, index) of class C (built on first use).
static const sv_elem_t *sv_elem (sv_t *v, sv_class_t *c, u32 h)
{
	if (!c->el)
	{
		for (uint f = 0; f < c->nf; f++)
			c->nel += v->fields[c->f0 + f].flags & 0x10 ? v->maxel : 256;
		c->el = MALLOC ((c->nel ? c->nel : 1) * sizeof (sv_elem_t));
		uint k = 0;
		for (uint f = 0; f < c->nf; f++)
		{
			const sv_field_t *fd = v->fields + c->f0 + f;
			const uint lim = fd->flags & 0x10 ? v->maxel : 256;
			for (uint i = 0; i < lim; i++, k++)
			{
				char tmp[16];
				const int n = snprintf (tmp, sizeof (tmp), "[%05u]", i);
				c->el[k].h = sv_fnv (fd->key, (const u8 *)tmp, (size_t)n, false);
				c->el[k].f = (u16)f, c->el[k].i = (u16)i;
			}
		}
		qsort (c->el, c->nel, sizeof (sv_elem_t), sv_elem_cmp);
	}
	sv_elem_t key = { h, 0, 0 };
	return c->nel ? bsearch (&key, c->el, c->nel, sizeof (sv_elem_t), sv_elem_cmp) : 0;
}

static void sv_load_names_dir (sv_t *v, ccp arg, u8 ***bufs, uint *nbufs)
{
	char dir[PATH_MAX], fn[PATH_MAX];
	if (!realpath (arg, dir))
		snprintf (dir, sizeof (dir), "%s", arg);
	char *sl = strrchr (dir, '/');
	if (!sl)
		return;
	*sl = 0;
	DIR *d = opendir (dir);
	if (d)
	{
		struct dirent *de;
		while ((de = readdir (d)))
		{
			if (!is_ext (de->d_name, ".bin"))
				continue;
			u8 *p = 0;
			size_t n = 0;
			snprintf (fn, sizeof (fn), "%s/%s", dir, de->d_name);
			if (!LoadFileAlloc (fn, 0, 0, &p, &n, 0, 2, 0, false) && p)
			{
				*bufs = REALLOC (*bufs, (*nbufs + 1) * sizeof (u8 *));
				(*bufs)[(*nbufs)++] = p;
				sv_add_strings (v, p, n);
			}
		}
		closedir (d);
	}
	for (int up = 0; up < 8; up++)
	{
		snprintf (fn, sizeof (fn), "%s/sys/main.dol", dir);
		u8 *p = 0;
		size_t n = 0;
		if (!LoadFileAlloc (fn, 0, 0, &p, &n, 0, 2, 0, false) && p)
		{
			*bufs = REALLOC (*bufs, (*nbufs + 1) * sizeof (u8 *));
			(*bufs)[(*nbufs)++] = p;
			sv_add_strings (v, p, n);
			break;
		}
		sl = strrchr (dir, '/');
		if (!sl)
			break;
		*sl = 0;
	}
}

static enumError extract_simsvault_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".vlt") || (export_count == 0 && !opt_decode)
		|| !file_has_magic (arg, "\0\0\0\001VALT", 8))
		return ERR_NOTHING_TO_DO;
	u8 *vb = 0, *bb = 0, **bufs = 0;
	uint nbufs = 0;
	size_t vsz = 0, bsz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	sv_t v;
	memset (&v, 0, sizeof (v));
	if (LoadFileAlloc (arg, 0, 0, &vb, &vsz, 0, 0, 0, false) || !vb || vsz < 0x28)
		goto done;
	{
		char bn[PATH_MAX];
		snprintf (bn, sizeof (bn), "%s", arg);
		char *dot = strrchr (bn, '.');
		snprintf (dot, sizeof (bn) - (dot - bn), ".bin");
		if (LoadFileAlloc (bn, 0, 0, &bb, &bsz, 0, 2, 0, false) || !bb || bsz < 0x40
			|| memcmp (bb + 4, "VBIN", 4))
			goto done;
	}
	const uint ver = rd_be32 (vb + 8);
	const u32 nc = rd_be32 (vb + 0x10);
	if (ver < 2 || ver > 3 || !nc || nc > vsz / 0x20)
		goto done;

	// type table (v3)
	size_t o = 0x24 + 4 * (size_t)nc;
	if (ver >= 3)
	{
		const u32 nt = rd_be32 (vb + 0x14);
		size_t mo = 0x28 + 0x18 * (size_t)nt;
		if (nt > vsz / 0x18 || mo > vsz)
			goto done;
		v.types = CALLOC (nt ? nt : 1, sizeof (sv_type_t));
		uint nmem = 0;
		for (u32 i = 0; i < nt; i++)
			nmem += vb[0x28 + 0x18 * i + 10] << 8 | vb[0x28 + 0x18 * i + 11];
		v.mkey = CALLOC (nmem + 1, sizeof (u32));
		v.mtype = CALLOC (nmem + 1, sizeof (u16));
		v.mcount = CALLOC (nmem + 1, sizeof (u16));
		uint m0 = 0;
		for (u32 i = 0; i < nt; i++)
		{
			const u8 *r = vb + 0x28 + 0x18 * i;
			sv_type_t *t = v.types + i;
			t->key = rd_be32 (r), t->size = r[4] << 8 | r[5], t->align = r[8] << 8 | r[9];
			t->nm = r[10] << 8 | r[11], t->m0 = m0;
			if (mo + 8 * (size_t)t->nm > vsz)
				goto done;
			for (uint j = 0; j < t->nm; j++)
			{
				v.mkey[m0 + j] = rd_be32 (vb + mo + 4 * j);
				v.mtype[m0 + j] = vb[mo + 4 * t->nm + 2 * j] << 8 | vb[mo + 4 * t->nm + 2 * j + 1];
				v.mcount[m0 + j] = vb[mo + 6 * t->nm + 2 * j] << 8 | vb[mo + 6 * t->nm + 2 * j + 1];
			}
			m0 += t->nm;
			mo += 8 * (size_t)t->nm;
		}
		v.ntypes = nt;
		o = mo + 4 * (size_t)nc;
	}

	// classes
	int mode = -1;
	for (int m = 0; m < (ver >= 3 ? 2 : 1) && mode < 0; m++)
		if (sv_walk_classes (&v, vb, vsz, o, ver, nc, m, false))
			mode = m;
	if (mode < 0)
		goto done;
	v.cls = CALLOC (nc, sizeof (sv_class_t));
	v.fields = CALLOC (vsz / 16 + 1, sizeof (sv_field_t));
	sv_walk_classes (&v, vb, vsz, o, ver, nc, mode, true);

	// collection layout
	const u32 ncoll = rd_be32 (bb + 12), nstr = rd_be32 (bb + 0x20);
	size_t start = 0, end = 0;
	uint H = 0, TL = 0;
	for (size_t s = 0x30; s < 0x400 && !H; s += 4)
		for (uint tl = 0; tl <= 4 && !H; tl += 4)
			for (uint h = 0x10; h < 0x34 && !H; h += 4)
			{
				if (!sv_chain (bb, bsz, s, h, tl, ncoll < 4 ? ncoll : 4))
					continue;
				const size_t e = sv_chain (bb, bsz, s, h, tl, ncoll);
				if (!e || e + 8 * (size_t)nstr > bsz)
					continue;
				if (nstr)
				{
					u32 j = 0;
					while (j + 1 < nstr && !rd_be32 (bb + e + 8 * j))
						j++;
					const u32 k = rd_be32 (bb + e + 8 * j), so = rd_be32 (bb + e + 8 * j + 4);
					const size_t pool = e + 8 * (size_t)nstr;
					if (so >= bsz - pool)
						continue;
					const size_t l = strnlen ((const char *)bb + pool + so, bsz - pool - so);
					if (sv_fnv (0x811c9dc5u, bb + pool + so, l, false) != k)
						continue;
				}
				start = s, end = e, H = h, TL = tl;
			}
	if (!H)
		goto done;
	size_t *starts = MALLOC ((ncoll + 1) * sizeof (size_t));
	v.maxel = 256;
	{
		size_t q = start;
		for (u32 i = 0; i < ncoll; i++)
		{
			starts[i] = q;
			const uint ne = bb[q + H] << 8 | bb[q + H + 1];
			if (ne > v.maxel)
				v.maxel = ne;
			q += H + 8 + (size_t)(bb[q + H + 2] << 8 | bb[q + H + 3]) * 8
				+ (bb[q + H + 6] << 8 | bb[q + H + 7]) + TL;
		}
	}
	// class word: the header position holding the most known class keys; key before it (or two
	// before when the word in between is the same vault key everywhere), parent after it
	uint co = 0, best = 0;
	for (uint j = 0; j + 4 <= H; j += 4)
	{
		uint hits = 0;
		for (u32 i = 0; i < ncoll; i++)
			hits += sv_class (&v, rd_be32 (bb + starts[i] + j)) != 0;
		if (hits > best)
			best = hits, co = j;
	}
	uint ko = co >= 4 ? co - 4 : 0;
	if (co >= 8 && ncoll > 1)
	{
		bool same = true;
		for (u32 i = 1; i < ncoll && same; i++)
			same = rd_be32 (bb + starts[i] + co - 4) == rd_be32 (bb + starts[0] + co - 4);
		if (same)
			ko = co - 8;
	}
	const uint po = co + 4;
	// entry word order
	bool swap = false;
	{
		uint hit[2] = { 0, 0 };
		for (u32 i = 0; i < ncoll && i < 400; i++)
		{
			const size_t x = starts[i];
			const sv_class_t *c = sv_class (&v, rd_be32 (bb + x + co));
			if (!c)
				continue;
			const uint n = bb[x + H] << 8 | bb[x + H + 1];
			for (uint j = 0; j < n; j++)
				for (uint w = 0; w < 2; w++)
				{
					const u32 k = rd_be32 (bb + x + H + 8 + TL + 8 * j + 4 * w);
					for (uint f = 0; f < c->nf; f++)
						if (v.fields[c->f0 + f].key == k)
						{
							hit[w]++;
							break;
						}
				}
		}
		swap = hit[1] > hit[0];
	}

	// names
	const size_t pool = end + 8 * (size_t)nstr;
	v.pool = bb + pool, v.poolsz = bsz - pool;
	v.strs = MALLOC ((nstr + 1) * sizeof (sv_str_t));
	for (u32 i = 0; i < nstr; i++)
	{
		const u32 so = rd_be32 (bb + end + 8 * i + 4);
		if (so >= v.poolsz)
			continue;
		v.strs[v.nstrs].h = rd_be32 (bb + end + 8 * i), v.strs[v.nstrs].off = so;
		v.nstrs++;
	}
	qsort (v.strs, v.nstrs, sizeof (sv_str_t), sv_u32_cmp);
	sv_add_strings (&v, bb, bsz);
	sv_load_names_dir (&v, arg, &bufs, &nbufs);
	for (uint i = 0; i < v.nstrs; i++)
		sv_add_name (&v, v.pool + v.strs[i].off,
			strnlen ((const char *)v.pool + v.strs[i].off, v.poolsz - v.strs[i].off));
	static const char letters[] = "abcdefghijklmnopqrstuvwxyz";
	for (uint i = 0; i < 26; i++)
		sv_add_name (&v, (const u8 *)letters + i, 1);
	qsort (v.names, v.nnames, sizeof (sv_name_t), sv_name_cmp);

	// classes
	char tmp[96];
	for (uint c = 0; c < v.ncls; c++)
	{
		sv_puts (&v, "# class ");
		sv_put_key (&v, v.cls[c].key);
		bcsv_put (&v.out, tmp, snprintf (tmp, sizeof (tmp), " (%u fields)\n", v.cls[c].nf));
		for (uint f = 0; f < v.cls[c].nf; f++)
		{
			const sv_field_t *fd = v.fields + v.cls[c].f0 + f;
			sv_puts (&v, "#   ");
			sv_put_key (&v, fd->type);
			bcsv_put (&v.out, " ", 1);
			sv_put_key (&v, fd->key);
			if (fd->flags & 0x10)
				sv_puts (&v, "[]");
			bcsv_put (&v.out, "\n", 1);
		}
	}
	bcsv_put (&v.out, "\n", 1);

	// collections
	u8 *vbuf = MALLOC (0x10000 + 16);
	u8 *seen = MALLOC (256);
	uint seencap = 256;
	for (u32 r = 0; r < ncoll; r++)
	{
		const size_t x = starts[r];
		const u32 key = rd_be32 (bb + x + ko), ck = rd_be32 (bb + x + co),
				  par = rd_be32 (bb + x + po);
		const uint n = bb[x + H] << 8 | bb[x + H + 1], cap = bb[x + H + 2] << 8 | bb[x + H + 3],
				   dcap = bb[x + H + 6] << 8 | bb[x + H + 7];
		const size_t eo = x + H + 8 + TL, db = eo + (size_t)cap * 8;
		sv_class_t *c = (sv_class_t *)sv_class (&v, ck);
		bcsv_put (&v.out, "[", 1);
		sv_put_key (&v, ck);
		bcsv_put (&v.out, "/", 1);
		sv_put_key (&v, key);
		bcsv_put (&v.out, "]", 1);
		if (par)
		{
			sv_puts (&v, " : ");
			sv_put_key (&v, par);
		}
		bcsv_put (&v.out, "\n", 1);
		// fields that have separate element entries in this collection
		if (c && c->nf > seencap)
		{
			seencap = c->nf;
			seen = REALLOC (seen, seencap);
		}
		if (c)
			memset (seen, 0, c->nf);
		for (uint j = 0; c && j < n; j++)
		{
			const u32 ek = rd_be32 (bb + eo + 8 * j + (swap ? 4 : 0));
			bool direct = false;
			for (uint f = 0; f < c->nf && !direct; f++)
				direct = v.fields[c->f0 + f].key == ek;
			const sv_elem_t *el = direct ? 0 : sv_elem (&v, c, ek);
			if (el)
				seen[el->f] = 1;
		}
		for (uint j = 0; j < n; j++)
		{
			const u32 ek = rd_be32 (bb + eo + 8 * j + (swap ? 4 : 0)),
					  val = rd_be32 (bb + eo + 8 * j + (swap ? 0 : 4));
			const sv_field_t *fd = 0;
			int idx = -1;
			uint fi = 0;
			for (uint f = 0; c && f < c->nf && !fd; f++)
				if (v.fields[c->f0 + f].key == ek)
					fd = v.fields + c->f0 + f, fi = f;
			if (!fd && c)
			{
				const sv_elem_t *el = sv_elem (&v, c, ek);
				if (el)
					fd = v.fields + c->f0 + el->f, fi = el->f, idx = el->i;
			}
			if (!fd)
			{
				sv_puts (&v, "  ? ");
				sv_put_key (&v, ek);
				bcsv_put (&v.out, tmp, snprintf (tmp, sizeof (tmp), " = 0x%08x\n", val));
				continue;
			}
			if (idx < 0 && seen[fi])
				continue;
			sv_puts (&v, "  ");
			sv_put_key (&v, fd->type);
			bcsv_put (&v.out, " ", 1);
			sv_put_key (&v, fd->key);
			if (idx >= 0)
				bcsv_put (&v.out, tmp, snprintf (tmp, sizeof (tmp), "[%d]", idx));
			sv_puts (&v, " = ");
			const bool arr = idx < 0 && (fd->flags & 0x10);
			const uint sz = fd->size;
			if (sz <= 4 && !arr)
			{
				memset (vbuf, 0, 32);
				wr_be32 (vbuf, val);
				sv_put_value (&v, fd->type, fd->ti, vbuf, sz, 0);
			}
			else
			{
				const uint off = val & 0xffff, cnt = val >> 16;
				const uint total = (arr ? cnt : 1) * sz;
				if ((size_t)off + total > dcap || db + off + total > bsz)
					sv_puts (&v, arr && !cnt ? "[]" : "<none>");
				else if (arr)
				{
					bcsv_put (&v.out, "[", 1);
					for (uint k = 0; k < cnt; k++)
					{
						if (k)
							sv_puts (&v, ", ");
						memset (vbuf, 0, sz + 16);
						memcpy (vbuf, bb + db + off + k * sz, sz);
						sv_put_value (&v, fd->type, fd->ti, vbuf, sz, 0);
					}
					bcsv_put (&v.out, "]", 1);
				}
				else
				{
					memset (vbuf, 0, sz + 16);
					memcpy (vbuf, bb + db + off, sz);
					sv_put_value (&v, fd->type, fd->ti, vbuf, sz, 0);
				}
			}
			bcsv_put (&v.out, "\n", 1);
		}
	}
	FREE (seen);
	FREE (vbuf);
	FREE (starts);

	if (v.out.p)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog,
				"%s%sEXTRACT SIMS-VAULT:%s -> %s/%sattrib.txt (%u classes, %u collections)\n",
				verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest,
				basedir ? basedir : "", v.ncls, ncoll);
		err = testmode
			? ERR_OK
			: shade_write (arg, dest, basedir, "attrib.txt", (u8 *)v.out.p, (uint)v.out.n);
	}
done:
	for (uint i = 0; i < v.ncls; i++)
		FREE (v.cls[i].el);
	for (uint i = 0; i < nbufs; i++)
		FREE (bufs[i]);
	FREE (bufs);
	FREE (v.out.p);
	FREE (v.names);
	FREE (v.types);
	FREE (v.mkey);
	FREE (v.mtype);
	FREE (v.mcount);
	FREE (v.fields);
	FREE (v.cls);
	FREE (v.strs);
	FREE (bb);
	FREE (vb);
	return err;
}
