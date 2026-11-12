// EA "Attrib" vault (`.vlt` + `.bin`, 64-bit keys): the gameplay database of the EA Playground /
// Celebrity Sports Showdown engine, FIFA 10-15, EA Sports Active, Skate It, NHL Slapshot
// (`data/db/db.vlt`, `data/attrib/db.vlt`, ...). The `.vlt` is a chunk list
// ({tag, u32 BE size including the 8-byte header}): `Vers`, `StrN`, `DepN` (u64 count, count u64
// name hashes, count u32 name offsets, names: "db.vlt", "db.bin"), `DatN` (the records), `ExpN`
// (u64 count, then {u64 id, u64 type, u32 size, u32 offset} per export) and `PtrN` (16-byte fix-ups
// {u32 location, u16 kind, u16 file, u64 target}: kind 2 selects the file the following locations
// are in, kind 3 = pointer at location -> target offset in `file`, kind 0 ends; file 0 = the .vlt,
// 1 = the .bin). The `.bin` starts with a `StrE` string pool and holds the class field tables,
// arrays and strings. Every key is Jenkins' lookup8 with the level 0xABCDEF0011223344 over the
// plain name; names are restored from the strings of the `.bin` and of the game's `sys/main.dol`
// (searched in the parent folders), unresolved keys print as hex.
// Exports: `Attrib::ClassLoadData` {u64 key, u32 collection count, u32 field count, ptr fields},
// fields being 24-byte {u64 key, u64 type, u32 size, u16 count, u8 flags (bit 0 = array), u8 log2
// alignment}; `Attrib::CollectionLoadData` {u64 key, u64 class, u64 parent, u32 reserve, u32 shift,
// u32 entry count, u16 type count, u16 type count, u64 layout, u64 type keys[], entries {u64 field,
// u32 value, u16 type index, u16 flags (0x4000 = value stored inline, else a pointer)}}. Arrays are
// {u16 count, u16 capacity, u16 element size, u16 pad} followed by the elements. Written as
// "attrib.txt": every class with its field definitions, then every collection ("[class/name] :
// parent") with its typed values. `Structs::*` members are not described by the file and are
// written as BE u32 words.
// EA Sports Active 2 and NFL Training Camp use the same layout with 32-bit keys (Jenkins' lookup2,
// init 0xABCDEF00): every u64 key, count and fix-up target above is a u32 there, so exports are 16
// bytes, fix-ups 12, fields 16, collection headers 32 and entries 12. The width is told apart by
// the size of `ExpN`.

typedef struct eaat_name_t
{
	u64 h;
	const char *s;
	uint n;
} eaat_name_t;
typedef struct eaat_fix_t
{
	u32 loc;
	u32 file;
	u64 dst;
} eaat_fix_t;
typedef struct eaat_t
{
	const u8 *f[2];
	size_t sz[2];
	eaat_fix_t *fix[2];
	uint nfix[2], capfix[2];
	eaat_name_t *names;
	uint nnames, capnames;
	uint w; // key width: 8 (lookup8) or 4 (lookup2)
	bcsv_buf_t out;
} eaat_t;

static u64 eaat_be64 (const u8 *p)
{
	return (u64)rd_be32 (p) << 32 | rd_be32 (p + 4);
}

#define EAAT_MIX(a, b, c)                                                                          \
	do                                                                                             \
	{                                                                                              \
		a -= b;                                                                                    \
		a -= c;                                                                                    \
		a ^= (c >> 43);                                                                            \
		b -= c;                                                                                    \
		b -= a;                                                                                    \
		b ^= (a << 9);                                                                             \
		c -= a;                                                                                    \
		c -= b;                                                                                    \
		c ^= (b >> 8);                                                                             \
		a -= b;                                                                                    \
		a -= c;                                                                                    \
		a ^= (c >> 38);                                                                            \
		b -= c;                                                                                    \
		b -= a;                                                                                    \
		b ^= (a << 23);                                                                            \
		c -= a;                                                                                    \
		c -= b;                                                                                    \
		c ^= (b >> 5);                                                                             \
		a -= b;                                                                                    \
		a -= c;                                                                                    \
		a ^= (c >> 35);                                                                            \
		b -= c;                                                                                    \
		b -= a;                                                                                    \
		b ^= (a << 49);                                                                            \
		c -= a;                                                                                    \
		c -= b;                                                                                    \
		c ^= (b >> 11);                                                                            \
		a -= b;                                                                                    \
		a -= c;                                                                                    \
		a ^= (c >> 12);                                                                            \
		b -= c;                                                                                    \
		b -= a;                                                                                    \
		b ^= (a << 18);                                                                            \
		c -= a;                                                                                    \
		c -= b;                                                                                    \
		c ^= (b >> 22);                                                                            \
	} while (0)

// Bob Jenkins' lookup8 hash (little-endian word loads), as used by Attrib with level
// 0xABCDEF0011223344.
static u64 eaat_hash (const u8 *k, uint len)
{
	u64 a = 0xABCDEF0011223344ull, b = a, c = 0x9e3779b97f4a7c13ull;
	uint n = len;
	while (n >= 24)
	{
		u64 w[3] = { 0, 0, 0 };
		for (int i = 0; i < 24; i++)
			w[i / 8] |= (u64)k[i] << (8 * (i % 8));
		a += w[0];
		b += w[1];
		c += w[2];
		EAAT_MIX (a, b, c);
		k += 24;
		n -= 24;
	}
	c += len;
	for (int j = (int)n - 1; j >= 0; j--)
	{
		if (j >= 16)
			c += (u64)k[j] << (8 * (j - 15));
		else if (j >= 8)
			b += (u64)k[j] << (8 * (j - 8));
		else
			a += (u64)k[j] << (8 * j);
	}
	EAAT_MIX (a, b, c);
	return c;
}
#undef EAAT_MIX

// Bob Jenkins' lookup2 hash, the 32-bit Attrib key (init 0xABCDEF00).
static u32 eaat_hash32 (const u8 *k, uint len)
{
	u32 a = 0x9e3779b9u, b = a, c = 0xABCDEF00u;
	uint n = len;
#define EAAT_MIX32(a, b, c)                                                                        \
	do                                                                                             \
	{                                                                                              \
		a -= b;                                                                                    \
		a -= c;                                                                                    \
		a ^= c >> 13;                                                                              \
		b -= c;                                                                                    \
		b -= a;                                                                                    \
		b ^= a << 8;                                                                               \
		c -= a;                                                                                    \
		c -= b;                                                                                    \
		c ^= b >> 13;                                                                              \
		a -= b;                                                                                    \
		a -= c;                                                                                    \
		a ^= c >> 12;                                                                              \
		b -= c;                                                                                    \
		b -= a;                                                                                    \
		b ^= a << 16;                                                                              \
		c -= a;                                                                                    \
		c -= b;                                                                                    \
		c ^= b >> 5;                                                                               \
		a -= b;                                                                                    \
		a -= c;                                                                                    \
		a ^= c >> 3;                                                                               \
		b -= c;                                                                                    \
		b -= a;                                                                                    \
		b ^= a << 10;                                                                              \
		c -= a;                                                                                    \
		c -= b;                                                                                    \
		c ^= b >> 15;                                                                              \
	} while (0)
	while (n >= 12)
	{
		a += k[0] | k[1] << 8 | k[2] << 16 | (u32)k[3] << 24;
		b += k[4] | k[5] << 8 | k[6] << 16 | (u32)k[7] << 24;
		c += k[8] | k[9] << 8 | k[10] << 16 | (u32)k[11] << 24;
		EAAT_MIX32 (a, b, c);
		k += 12;
		n -= 12;
	}
	c += len;
	for (int j = (int)n - 1; j >= 0; j--)
	{
		if (j >= 8)
			c += (u32)k[j] << (8 * (j - 7));
		else if (j >= 4)
			b += (u32)k[j] << (8 * (j - 4));
		else
			a += (u32)k[j] << (8 * j);
	}
	EAAT_MIX32 (a, b, c);
#undef EAAT_MIX32
	return c;
}

// A key, count or target of the vault's width.
static u64 eaat_key (const eaat_t *v, const u8 *p)
{
	return v->w == 8 ? eaat_be64 (p) : rd_be32 (p);
}

static int eaat_name_cmp (const void *x, const void *y)
{
	const u64 a = ((const eaat_name_t *)x)->h, b = ((const eaat_name_t *)y)->h;
	return a < b ? -1 : a > b;
}

static int eaat_fix_cmp (const void *x, const void *y)
{
	const u32 a = ((const eaat_fix_t *)x)->loc, b = ((const eaat_fix_t *)y)->loc;
	return a < b ? -1 : a > b;
}

// Adds every NUL- or non-printable-terminated run of 2+ printable characters as a name candidate.
static void eaat_add_strings (eaat_t *v, const u8 *p, size_t n)
{
	size_t i = 0;
	while (i < n)
	{
		size_t j = i;
		while (j < n && p[j] >= 0x20 && p[j] < 0x7f)
			j++;
		if (j - i >= 2 && j - i < 256)
		{
			if (v->nnames == v->capnames)
			{
				v->capnames = v->capnames * 2 + 4096;
				v->names = REALLOC (v->names, v->capnames * sizeof (*v->names));
			}
			eaat_name_t *e = v->names + v->nnames++;
			e->h
				= v->w == 8 ? eaat_hash (p + i, (uint)(j - i)) : eaat_hash32 (p + i, (uint)(j - i));
			e->s = (const char *)p + i;
			e->n = (uint)(j - i);
		}
		i = j + 1;
	}
}

static const eaat_name_t *eaat_lookup (const eaat_t *v, u64 h)
{
	eaat_name_t key = { h, 0, 0 };
	return v->nnames ? bsearch (&key, v->names, v->nnames, sizeof (*v->names), eaat_name_cmp) : 0;
}

static void eaat_put_key (eaat_t *v, u64 h)
{
	char tmp[24];
	const eaat_name_t *e = eaat_lookup (v, h);
	if (e)
		bcsv_put (&v->out, e->s, e->n);
	else
	{
		const int n = v->w == 8 ? snprintf (tmp, sizeof (tmp), "0x%016llx", (unsigned long long)h)
								: snprintf (tmp, sizeof (tmp), "0x%08x", (u32)h);
		bcsv_put (&v->out, tmp, n);
	}
}

// Short type name: the part after the last "::".
static void eaat_put_type (eaat_t *v, u64 h)
{
	const eaat_name_t *e = eaat_lookup (v, h);
	if (!e)
	{
		eaat_put_key (v, h);
		return;
	}
	uint s = 0;
	for (uint i = 0; i + 1 < e->n; i++)
		if (e->s[i] == ':' && e->s[i + 1] == ':')
			s = i + 2;
	bcsv_put (&v->out, e->s + s, e->n - s);
}

static void eaat_puts (eaat_t *v, const char *s)
{
	bcsv_put (&v->out, s, strlen (s));
}

static const eaat_fix_t *eaat_fix (const eaat_t *v, uint file, u32 loc)
{
	eaat_fix_t key = { loc, 0, 0 };
	return file < 2 && v->nfix[file]
		? bsearch (&key, v->fix[file], v->nfix[file], sizeof (eaat_fix_t), eaat_fix_cmp)
		: 0;
}

static bool eaat_type_is (const eaat_name_t *t, ccp suffix)
{
	const uint n = (uint)strlen (suffix);
	return t && t->n >= n && !memcmp (t->s + t->n - n, suffix, n);
}

// Writes one value of type TYPE, SIZE bytes at FILE:OFF.
static void eaat_put_value (eaat_t *v, u64 type, uint file, size_t off, uint size)
{
	char tmp[64];
	if (file > 1 || off > v->sz[file] || size > v->sz[file] - off)
	{
		eaat_puts (v, "<out of range>");
		return;
	}
	const u8 *p = v->f[file] + off;
	const eaat_name_t *t = eaat_lookup (v, type);
	int n = -1;
	if (eaat_type_is (t, "::Float") && size >= 4)
	{
		const u32 u = rd_be32 (p);
		float x;
		memcpy (&x, &u, 4);
		n = snprintf (tmp, sizeof (tmp), "%g", x);
	}
	else if (eaat_type_is (t, "::Double") && size >= 8)
	{
		const u64 u = eaat_be64 (p);
		double x;
		memcpy (&x, &u, 8);
		n = snprintf (tmp, sizeof (tmp), "%g", x);
	}
	else if (eaat_type_is (t, "::Bool") && size >= 1)
		n = snprintf (tmp, sizeof (tmp), "%s", p[0] ? "true" : "false");
	else if ((eaat_type_is (t, "Int8") || eaat_type_is (t, "Int16") || eaat_type_is (t, "Int32")
				 || eaat_type_is (t, "Int64"))
		&& t->n > 6)
	{
		const bool uns = t->s[t->n - (eaat_type_is (t, "Int8") ? 5 : 6)] == 'U';
		const uint bytes = eaat_type_is (t, "Int8") ? 1
			: eaat_type_is (t, "Int16")				? 2
			: eaat_type_is (t, "Int32")				? 4
													: 8;
		if (bytes <= size)
		{
			u64 u = 0;
			for (uint i = 0; i < bytes; i++)
				u = u << 8 | p[i];
			if (!uns && bytes < 8 && (u >> (bytes * 8 - 1) & 1))
				u |= ~0ull << (bytes * 8);
			n = uns ? snprintf (tmp, sizeof (tmp), "%llu", (unsigned long long)u)
					: snprintf (tmp, sizeof (tmp), "%lld", (long long)u);
		}
	}
	else if (eaat_type_is (t, "::Text") && size >= 4)
	{
		const eaat_fix_t *fx = eaat_fix (v, file, (u32)off);
		if (!fx || fx->file > 1 || fx->dst >= v->sz[fx->file])
		{
			eaat_puts (v, rd_be32 (p) ? "<unresolved text>" : "\"\"");
			return;
		}
		const char *s = (const char *)v->f[fx->file] + fx->dst;
		size_t l = 0;
		while (fx->dst + l < v->sz[fx->file] && s[l])
			l++;
		bcsv_put (&v->out, "\"", 1);
		for (size_t i = 0; i < l; i++)
		{
			if (s[i] == '"' || s[i] == '\\')
				bcsv_put (&v->out, "\\", 1);
			if (s[i] == '\n')
				eaat_puts (v, "\\n");
			else
				bcsv_put (&v->out, s + i, 1);
		}
		bcsv_put (&v->out, "\"", 1);
		return;
	}
	else if ((eaat_type_is (t, "Vector2") || eaat_type_is (t, "Vector3")
				 || eaat_type_is (t, "Vector4") || eaat_type_is (t, "FloatColour"))
		&& size >= 8)
	{
		bcsv_put (&v->out, "(", 1);
		for (uint i = 0; i + 4 <= size; i += 4)
		{
			const u32 u = rd_be32 (p + i);
			float x;
			memcpy (&x, &u, 4);
			const int m = snprintf (tmp, sizeof (tmp), "%s%g", i ? ", " : "", x);
			bcsv_put (&v->out, tmp, m);
		}
		bcsv_put (&v->out, ")", 1);
		return;
	}
	else if (eaat_type_is (t, "Attrib::RefSpec") && size >= 2 * v->w)
	{
		eaat_put_key (v, eaat_key (v, p));
		bcsv_put (&v->out, "/", 1);
		eaat_put_key (v, eaat_key (v, p + v->w));
		return;
	}
	else if (t && t->n > 7 && !memcmp (t->s, "Enums::", 7) && size >= 4)
		n = snprintf (tmp, sizeof (tmp), "%d", (int)rd_be32 (p));
	if (n >= 0)
	{
		bcsv_put (&v->out, tmp, n);
		return;
	}
	// Unknown (Structs::*): BE u32 words, trailing bytes in hex.
	bcsv_put (&v->out, "{", 1);
	uint i = 0;
	for (; i + 4 <= size; i += 4)
	{
		const int m = snprintf (tmp, sizeof (tmp), "%s%08x", i ? " " : "", rd_be32 (p + i));
		bcsv_put (&v->out, tmp, m);
	}
	for (; i < size; i++)
	{
		const int m = snprintf (tmp, sizeof (tmp), "%s%02x", i && !(i & 3) ? " " : "", p[i]);
		bcsv_put (&v->out, tmp, m);
	}
	bcsv_put (&v->out, "}", 1);
}

typedef struct eaat_field_t
{
	u64 cls, key, type;
	u32 size;
	u16 count;
	u8 flags, align;
} eaat_field_t;

static const eaat_field_t *eaat_field (const eaat_field_t *fd, uint nfd, u64 cls, u64 key)
{
	for (uint i = 0; i < nfd; i++)
		if (fd[i].cls == cls && fd[i].key == key)
			return fd + i;
	return 0;
}

// Loads "<dir>/<name>" (dir taken from PATH) into *buf.
static bool eaat_load_sibling (ccp path, ccp name, u8 **buf, size_t *sz)
{
	char fn[PATH_MAX];
	ccp sl = strrchr (path, '/');
	if (sl)
		snprintf (fn, sizeof (fn), "%.*s/%s", (int)(sl - path), path, name);
	else
		snprintf (fn, sizeof (fn), "%s", name);
	return !LoadFileAlloc (fn, 0, 0, buf, sz, 0, 2, 0, false) && *buf;
}

static enumError extract_eaattrib_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".vlt") || (export_count == 0 && !opt_decode)
		|| !file_has_magic (arg, "Vers", 4))
		return ERR_NOTHING_TO_DO;
	u8 *vb = 0, *bb = 0, *dol = 0;
	size_t vsz = 0, bsz = 0, dolsz = 0;
	if (LoadFileAlloc (arg, 0, 0, &vb, &vsz, 0, 0, 0, false) || !vb || vsz < 16)
	{
		FREE (vb);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	eaat_t v;
	memset (&v, 0, sizeof (v));
	eaat_field_t *fd = 0;
	uint nfd = 0;

	// chunk walk
	size_t dep = 0, exp = 0, ptr = 0, depsz = 0, expsz = 0, ptrsz = 0;
	for (size_t o = 0; o + 8 <= vsz;)
	{
		const u32 csz = rd_be32 (vb + o + 4);
		if (csz < 8 || csz > vsz - o)
			break;
		if (!memcmp (vb + o, "DepN", 4))
			dep = o, depsz = csz;
		else if (!memcmp (vb + o, "ExpN", 4))
			exp = o, expsz = csz;
		else if (!memcmp (vb + o, "PtrN", 4))
			ptr = o, ptrsz = csz;
		o += csz;
	}
	if (!exp || !ptr || expsz < 16)
		goto done;
	for (uint w = 8; w >= 4 && !v.w; w -= 4)
	{
		const u64 n = w == 8 ? eaat_be64 (vb + exp + 8) : rd_be32 (vb + exp + 8);
		const u64 used = 8 + w + n * (2 * w + 8);
		if (n && used <= expsz && used + 16 > expsz)
			v.w = w;
	}
	if (!v.w)
		goto done;
	const uint W = v.w;

	// dependency 1 = the .bin (name from DepN, else <stem>.bin)
	{
		char binname[256] = "";
		if (dep && depsz >= 16)
		{
			const u64 nd = eaat_key (&v, vb + dep + 8);
			const size_t names = dep + 8 + W + nd * (W + 4);
			if (nd >= 2 && nd < 16 && names < dep + depsz)
			{
				const u32 no = rd_be32 (vb + dep + 8 + W + nd * W + 4);
				if (names + no < dep + depsz)
					snprintf (binname, sizeof (binname), "%.*s",
						(int)strnlen ((const char *)vb + names + no, dep + depsz - names - no),
						(const char *)vb + names + no);
			}
		}
		if (!*binname || strchr (binname, '/'))
		{
			ccp sl = strrchr (arg, '/');
			snprintf (binname, sizeof (binname), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (binname, '.');
			if (dot)
				snprintf (dot, sizeof (binname) - (dot - binname), ".bin");
		}
		if (!eaat_load_sibling (arg, binname, &bb, &bsz) || bsz < 8 || memcmp (bb, "StrE", 4))
			goto done;
	}
	v.f[0] = vb, v.sz[0] = vsz, v.f[1] = bb, v.sz[1] = bsz;

	// fix-ups
	{
		uint cur = 2;
		for (size_t e = ptr + 8; e + 8 + W <= ptr + ptrsz; e += 8 + W)
		{
			const u32 loc = rd_be32 (vb + e);
			const uint kind = vb[e + 4] << 8 | vb[e + 5], file = vb[e + 6] << 8 | vb[e + 7];
			if (kind == 0)
				break;
			if (kind == 2)
				cur = file < 2 ? file : 2;
			else if (kind == 3 && cur < 2 && file < 2)
			{
				if (v.nfix[cur] == v.capfix[cur])
				{
					v.capfix[cur] = v.capfix[cur] * 2 + 1024;
					v.fix[cur] = REALLOC (v.fix[cur], v.capfix[cur] * sizeof (eaat_fix_t));
				}
				eaat_fix_t *x = v.fix[cur] + v.nfix[cur]++;
				x->loc = loc, x->file = file, x->dst = eaat_key (&v, vb + e + 8);
			}
		}
		for (uint i = 0; i < 2; i++)
			if (v.nfix[i])
				qsort (v.fix[i], v.nfix[i], sizeof (eaat_fix_t), eaat_fix_cmp);
	}

	// names: .bin strings + the game's sys/main.dol (searched upward)
	eaat_add_strings (&v, bb, bsz);
	{
		char dir[PATH_MAX], fn[PATH_MAX];
		if (!realpath (arg, dir))
			snprintf (dir, sizeof (dir), "%s", arg);
		for (int up = 0; up < 8 && !dol; up++)
		{
			char *sl = strrchr (dir, '/');
			if (!sl)
				break;
			*sl = 0;
			snprintf (fn, sizeof (fn), "%s/sys/main.dol", dir);
			if (LoadFileAlloc (fn, 0, 0, &dol, &dolsz, 0, 2, 0, false) || !dol)
				dol = 0, dolsz = 0;
		}
		if (dol)
			eaat_add_strings (&v, dol, dolsz);
	}
	static const char *const builtin[]
		= { "Attrib::ClassLoadData", "Attrib::CollectionLoadData", "Attrib::DatabaseLoadData" };
	eaat_add_strings (&v, (const u8 *)builtin[0], strlen (builtin[0]));
	eaat_add_strings (&v, (const u8 *)builtin[1], strlen (builtin[1]));
	eaat_add_strings (&v, (const u8 *)builtin[2], strlen (builtin[2]));
	if (v.nnames)
		qsort (v.names, v.nnames, sizeof (*v.names), eaat_name_cmp);
	const u64 t_class = W == 8 ? eaat_hash ((const u8 *)builtin[0], (uint)strlen (builtin[0]))
							   : eaat_hash32 ((const u8 *)builtin[0], (uint)strlen (builtin[0]));
	const u64 t_coll = W == 8 ? eaat_hash ((const u8 *)builtin[1], (uint)strlen (builtin[1]))
							  : eaat_hash32 ((const u8 *)builtin[1], (uint)strlen (builtin[1]));
	const uint XR = 2 * W + 8, FR = 2 * W + 8, ER = W + 8; // export, field and entry record sizes
	const u64 nexp = eaat_key (&v, vb + exp + 8);
	const size_t xbase = exp + 8 + W;

	// classes
	uint nclass = 0, ncoll = 0;
	for (int pass = 0; pass < 2; pass++)
		for (u64 i = 0; i < nexp; i++)
		{
			const u8 *x = vb + xbase + i * XR;
			const u64 type = eaat_key (&v, x + W);
			const u32 esz = rd_be32 (x + 2 * W), off = rd_be32 (x + 2 * W + 4);
			if (off > vsz || esz > vsz - off)
				continue;
			if (type == t_coll && pass == 0)
				ncoll++;
			if (type != t_class || esz < W + 12)
				continue;
			const u64 ck = eaat_key (&v, vb + off);
			const u32 nf = rd_be32 (vb + off + W + 4);
			const eaat_fix_t *fx = eaat_fix (&v, 0, off + W + 8);
			if (!fx || nf > 0x10000 || fx->dst > v.sz[fx->file]
				|| (u64)nf * FR > v.sz[fx->file] - fx->dst)
				continue;
			const u8 *fp = v.f[fx->file] + fx->dst;
			if (pass == 0)
			{
				nclass++;
				fd = REALLOC (fd, (nfd + nf) * sizeof (*fd));
				for (u32 j = 0; j < nf; j++, nfd++)
				{
					const u8 *r = fp + j * FR;
					fd[nfd].cls = ck, fd[nfd].key = eaat_key (&v, r),
					fd[nfd].type = eaat_key (&v, r + W);
					fd[nfd].size = rd_be32 (r + 2 * W),
					fd[nfd].count = r[2 * W + 4] << 8 | r[2 * W + 5], fd[nfd].flags = r[2 * W + 6],
					fd[nfd].align = r[2 * W + 7];
				}
				continue;
			}
			char tmp[96];
			eaat_puts (&v, "# class ");
			eaat_put_key (&v, ck);
			const int m = snprintf (
				tmp, sizeof (tmp), " (%u fields, %u collections)\n", nf, rd_be32 (vb + off + W));
			bcsv_put (&v.out, tmp, m);
			for (u32 j = 0; j < nf; j++)
			{
				const u8 *r = fp + j * FR;
				const uint cnt = r[2 * W + 4] << 8 | r[2 * W + 5];
				eaat_puts (&v, "#   ");
				eaat_put_type (&v, eaat_key (&v, r + W));
				bcsv_put (&v.out, " ", 1);
				eaat_put_key (&v, eaat_key (&v, r));
				const int k = r[2 * W + 6] & 1 ? cnt == 0xffff
						? snprintf (tmp, sizeof (tmp), "[]")
						: snprintf (tmp, sizeof (tmp), "[%u]", cnt)
											   : 0;
				bcsv_put (&v.out, tmp, k);
				bcsv_put (&v.out, "\n", 1);
			}
		}
	if (!nclass || !ncoll)
		goto done;
	bcsv_put (&v.out, "\n", 1);

	// collections
	for (u64 i = 0; i < nexp; i++)
	{
		const u8 *x = vb + xbase + i * XR;
		const u32 esz = rd_be32 (x + 2 * W), off = rd_be32 (x + 2 * W + 4);
		if (eaat_key (&v, x + W) != t_coll || off > vsz || esz > vsz - off || esz < 4 * W + 16)
			continue;
		const u64 key = eaat_key (&v, vb + off), cls = eaat_key (&v, vb + off + W),
				  par = eaat_key (&v, vb + off + 2 * W);
		const u32 ne = rd_be32 (vb + off + 3 * W + 8);
		const uint tl = vb[off + 3 * W + 14] << 8 | vb[off + 3 * W + 15];
		const size_t tb = off + 4 * W + 16, eo = tb + (size_t)tl * W;
		if (eo + (size_t)ne * ER > (size_t)off + esz)
			continue;
		bcsv_put (&v.out, "[", 1);
		eaat_put_key (&v, cls);
		bcsv_put (&v.out, "/", 1);
		eaat_put_key (&v, key);
		bcsv_put (&v.out, "]", 1);
		if (par)
		{
			eaat_puts (&v, " : ");
			eaat_put_key (&v, par);
		}
		bcsv_put (&v.out, "\n", 1);
		for (u32 j = 0; j < ne; j++)
		{
			const u8 *r = vb + eo + j * ER;
			const u64 fk = eaat_key (&v, r);
			const uint ti = r[W + 4] << 8 | r[W + 5], nfl = r[W + 6] << 8 | r[W + 7];
			const u64 type = ti < tl ? eaat_key (&v, vb + tb + ti * W) : 0;
			const eaat_field_t *f = eaat_field (fd, nfd, cls, fk);
			const uint fsz = f ? f->size : 4;
			const u32 loc = (u32)(eo + j * ER + W);
			eaat_puts (&v, "  ");
			eaat_put_type (&v, type);
			bcsv_put (&v.out, " ", 1);
			eaat_put_key (&v, fk);
			eaat_puts (&v, " = ");
			if (nfl & 0x4000)
				eaat_put_value (&v, type, 0, loc, fsz < 4 ? fsz : 4);
			else
			{
				const eaat_fix_t *fx = eaat_fix (&v, 0, loc);
				if (!fx)
					eaat_puts (&v, "<null>");
				else if (f && (f->flags & 1))
				{
					const uint fl = fx->file;
					if (fx->dst + 8 > v.sz[fl])
						eaat_puts (&v, "<out of range>");
					else
					{
						const u8 *a = v.f[fl] + fx->dst;
						const uint cnt = a[0] << 8 | a[1], es = a[4] << 8 | a[5];
						size_t base = fx->dst + 8;
						const size_t al = (size_t)1 << (f->align < 6 ? f->align : 6);
						if (es >= 8)
							base = (base + al - 1) / al * al;
						bcsv_put (&v.out, "[", 1);
						for (uint k = 0; k < cnt; k++)
						{
							if (k)
								eaat_puts (&v, ", ");
							eaat_put_value (&v, type, fl, base + (size_t)k * es, es);
						}
						bcsv_put (&v.out, "]", 1);
					}
				}
				else
					eaat_put_value (&v, type, fx->file, (size_t)fx->dst, fsz);
			}
			bcsv_put (&v.out, "\n", 1);
		}
	}

	if (v.out.p)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog,
				"%s%sEXTRACT EA-ATTRIB:%s -> %s/%sattrib.txt (%u classes, %u collections)\n",
				verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest,
				basedir ? basedir : "", nclass, ncoll);
		err = testmode
			? ERR_OK
			: shade_write (arg, dest, basedir, "attrib.txt", (u8 *)v.out.p, (uint)v.out.n);
	}
done:
	FREE (v.out.p);
	FREE (v.names);
	FREE (v.fix[0]);
	FREE (v.fix[1]);
	FREE (fd);
	FREE (dol);
	FREE (bb);
	FREE (vb);
	return err;
}
