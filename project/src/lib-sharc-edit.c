#include "lib-sharc.h"
#include "lib-std.h"
#include "lib-gtx.h"

#include <sys/stat.h>
#ifndef __MINGW32__
  #include <sys/wait.h>
#endif

// SHARC (agl::ResShaderArchive) <-> directory, and directory -> SHARCFB
// (agl::ResBinaryShaderArchive, Wii U, version 8).
//
// Layout knowledge and the SHARCFB assembly follow aboood40091's SharcEditor
// (sharc.py: v10/11/12 source archive) and SharcCompiler (sharcfb.py /
// gx2shader.py / main.py: GSH -> SHARCFB).
//
// The extracted directory is:
//	sharc.txt          manifest: version, endianness, archive name, programs
//	                   (shader indices + the three editable macro lists), sources
//	sources/<name>     one shader source file per archive source, raw bytes
//	programs/NNN.bin   everything else in that program entry (variation and symbol
//	                   tables), kept verbatim so unknown fields survive a round trip
//
// EXTRACT verifies that rebuilding the directory reproduces the input byte for
// byte and refuses to write anything otherwise.

///////////////////////////////////////////////////////////////////////////////
// byte helpers

static inline u32 rd32 (const u8 *p, bool be)
{
	return be ? (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]
			  : (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

typedef struct sbuf_t
{
	u8 *p;
	uint n, cap;
	bool be;
} sbuf_t;

static bool sb_grow (sbuf_t *b, uint add)
{
	if (b->n + add <= b->cap)
		return true;
	uint cap = b->cap ? b->cap : 256;
	while (cap < b->n + add)
		cap *= 2;
	u8 *p = REALLOC (b->p, cap);
	if (!p)
		return false;
	b->p = p;
	b->cap = cap;
	return true;
}

static bool sb_put (sbuf_t *b, const void *data, uint size)
{
	if (!sb_grow (b, size))
		return false;
	if (size)
		memcpy (b->p + b->n, data, size);
	b->n += size;
	return true;
}

static bool sb_u32 (sbuf_t *b, u32 v)
{
	u8 t[4];
	if (b->be)
		t[0] = v >> 24, t[1] = v >> 16, t[2] = v >> 8, t[3] = v;
	else
		t[3] = v >> 24, t[2] = v >> 16, t[1] = v >> 8, t[0] = v;
	return sb_put (b, t, 4);
}

static void sb_set32 (sbuf_t *b, uint pos, u32 v)
{
	const uint n = b->n;
	b->n = pos;
	sb_u32 (b, v);
	b->n = n;
}

///////////////////////////////////////////////////////////////////////////////
// SHARC model

typedef struct sharc_macro_t
{
	char *name, *value;
} sharc_macro_t;

typedef struct sharc_prog_t
{
	char *name;
	s32 shader[3]; // vertex, fragment, geometry source index or -1
	sharc_macro_t *macro[3];
	uint n_macro[3];
	u8 *rest; // everything after the geometry macro list
	uint rest_size;
} sharc_prog_t;

typedef struct sharc_src_t
{
	char *name;
	u8 *code;
	uint code_size;
} sharc_src_t;

typedef struct sharc_t
{
	bool be;
	u32 version;
	char *name;
	sharc_prog_t *prog;
	uint n_prog;
	sharc_src_t *src;
	uint n_src;
} sharc_t;

static void sharc_free (sharc_t *s)
{
	for (uint i = 0; i < s->n_prog; i++)
	{
		sharc_prog_t *p = s->prog + i;
		FREE (p->name);
		for (uint k = 0; k < 3; k++)
		{
			for (uint m = 0; m < p->n_macro[k]; m++)
			{
				FREE (p->macro[k][m].name);
				FREE (p->macro[k][m].value);
			}
			FREE (p->macro[k]);
		}
		FREE (p->rest);
	}
	for (uint i = 0; i < s->n_src; i++)
	{
		FREE (s->src[i].name);
		FREE (s->src[i].code);
	}
	FREE (s->prog);
	FREE (s->src);
	FREE (s->name);
	memset (s, 0, sizeof (*s));
}

// String stored as [len bytes, the last one NUL]. Returns a malloc'ed copy
// without the NUL, or NULL if the field isn't a clean C string.
static char *dup_field (const u8 *p, u32 len)
{
	if (!len || p[len - 1] || memchr (p, 0, len - 1))
		return 0;
	return STRDUP ((ccp)p);
}

// Parses one size-prefixed list. ITEM(item_ptr,item_size) is called per
// entry. Returns the list's byte size or 0 on malformed data.
static uint list_extent (const u8 *p, const u8 *end, bool be, uint *count)
{
	if (end - p < 8)
		return 0;
	const u32 size = rd32 (p, be);
	if (size < 8 || size > (uint)(end - p))
		return 0;
	*count = rd32 (p + 4, be);
	return size;
}

static enumError sharc_parse (sharc_t *s, const u8 *data, size_t size)
{
	memset (s, 0, sizeof (*s));
	if (size < 20 || size > 0x7fffffff)
		return ERR_INVALID_DATA;
	if (!memcmp (data, "AAHS", 4))
		s->be = false;
	else if (!memcmp (data, "SHAA", 4))
		s->be = true;
	else
		return ERR_INVALID_DATA;
	const bool be = s->be;
	s->version = rd32 (data + 4, be);
	if (s->version < 10 || s->version > 12)
		return ERROR0 (ERR_INVALID_DATA, "SHARC: unsupported version %u\n", s->version);
	if (rd32 (data + 8, be) != size || rd32 (data + 12, be) != (be ? 0u : 1u))
		return ERROR0 (ERR_INVALID_DATA, "SHARC: file size or byte order mark mismatch\n");
	const u32 name_len = rd32 (data + 16, be);
	if (name_len > size - 20 || !(s->name = dup_field (data + 20, name_len)))
		return ERR_INVALID_DATA;

	const u8 *end = data + size, *p = data + 20 + name_len;
	uint n_prog = 0, prog_size = list_extent (p, end, be, &n_prog);
	if (!prog_size || n_prog > prog_size / 20)
		return ERR_INVALID_DATA;
	s->prog = CALLOC (n_prog ? n_prog : 1, sizeof (*s->prog));
	const u8 *pend = p + prog_size;
	const u8 *q = p + 8;
	enumError err = ERR_OK;
	for (uint i = 0; i < n_prog; i++)
	{
		if (pend - q < 20)
			return ERR_INVALID_DATA;
		const u32 esz = rd32 (q, be), nlen = rd32 (q + 4, be);
		if (esz < 20 + nlen || esz > (uint)(pend - q) || nlen > esz)
			return ERR_INVALID_DATA;
		sharc_prog_t *pr = s->prog + s->n_prog++;
		pr->shader[0] = (s32)rd32 (q + 8, be);
		pr->shader[1] = (s32)rd32 (q + 12, be);
		pr->shader[2] = (s32)rd32 (q + 16, be);
		if (!(pr->name = dup_field (q + 20, nlen)))
			return ERR_INVALID_DATA;
		const u8 *r = q + 20 + nlen, *qend = q + esz;
		for (uint k = 0; k < 3; k++)
		{
			uint cnt = 0, lsz = list_extent (r, qend, be, &cnt);
			if (!lsz || cnt > lsz / 12)
				return ERR_INVALID_DATA;
			pr->macro[k] = CALLOC (cnt ? cnt : 1, sizeof (*pr->macro[k]));
			const u8 *m = r + 8, *mend = r + lsz;
			for (uint j = 0; j < cnt; j++)
			{
				if (mend - m < 12)
					return ERR_INVALID_DATA;
				const u32 msz = rd32 (m, be), ml = rd32 (m + 4, be), vl = rd32 (m + 8, be);
				if (ml > msz || vl > msz || msz != 12 + ml + vl || msz > (uint)(mend - m))
					return ERR_INVALID_DATA;
				sharc_macro_t *mac = pr->macro[k] + pr->n_macro[k]++;
				mac->name = dup_field (m + 12, ml);
				mac->value = dup_field (m + 12 + ml, vl);
				if (!mac->name || !mac->value || strpbrk (mac->name, "\t\r\n")
					|| strpbrk (mac->value, "\t\r\n"))
					return ERR_INVALID_DATA;
				m += msz;
			}
			if (m != mend)
				return ERR_INVALID_DATA;
			r += lsz;
		}
		pr->rest_size = qend - r;
		pr->rest = MALLOC (pr->rest_size ? pr->rest_size : 1);
		memcpy (pr->rest, r, pr->rest_size);
		q += esz;
	}
	if (q != pend)
		return ERR_INVALID_DATA;

	p = pend;
	uint n_src = 0, src_size = list_extent (p, end, be, &n_src);
	if (!src_size || p + src_size != end || n_src > src_size / 16)
		return ERR_INVALID_DATA;
	s->src = CALLOC (n_src ? n_src : 1, sizeof (*s->src));
	q = p + 8;
	for (uint i = 0; i < n_src; i++)
	{
		if (end - q < 16)
			return ERR_INVALID_DATA;
		const u32 esz = rd32 (q, be), nlen = rd32 (q + 4, be), clen = rd32 (q + 8, be),
				  clen2 = rd32 (q + 12, be);
		if (clen != clen2 || nlen > esz || clen > esz || esz != 16 + nlen + clen
			|| esz > (uint)(end - q))
			return ERR_INVALID_DATA;
		sharc_src_t *sr = s->src + s->n_src++;
		if (!(sr->name = dup_field (q + 16, nlen)))
			return ERR_INVALID_DATA;
		sr->code_size = clen;
		sr->code = MALLOC (clen ? clen : 1);
		memcpy (sr->code, q + 16 + nlen, clen);
		q += esz;
	}
	if (q != end)
		return ERR_INVALID_DATA;
	return err;
}

static bool sb_str (sbuf_t *b, ccp s)
{
	return sb_put (b, s, strlen (s) + 1);
}

// Serialises S; the result is malloc'ed in *OUT.
static enumError sharc_build (const sharc_t *s, u8 **out, uint *out_size)
{
	sbuf_t b = { 0, 0, 0, s->be };
	bool ok = sb_put (&b, s->be ? "SHAA" : "AAHS", 4) && sb_u32 (&b, s->version)
		&& sb_u32 (&b, 0) && sb_u32 (&b, s->be ? 0 : 1) && sb_u32 (&b, strlen (s->name) + 1)
		&& sb_str (&b, s->name);

	const uint plist = b.n;
	ok = ok && sb_u32 (&b, 0) && sb_u32 (&b, s->n_prog);
	for (uint i = 0; ok && i < s->n_prog; i++)
	{
		const sharc_prog_t *p = s->prog + i;
		const uint start = b.n;
		ok = sb_u32 (&b, 0) && sb_u32 (&b, strlen (p->name) + 1) && sb_u32 (&b, p->shader[0])
			&& sb_u32 (&b, p->shader[1]) && sb_u32 (&b, p->shader[2]) && sb_str (&b, p->name);
		for (uint k = 0; ok && k < 3; k++)
		{
			const uint ls = b.n;
			ok = sb_u32 (&b, 0) && sb_u32 (&b, p->n_macro[k]);
			for (uint m = 0; ok && m < p->n_macro[k]; m++)
			{
				const sharc_macro_t *mac = p->macro[k] + m;
				const uint nl = strlen (mac->name) + 1, vl = strlen (mac->value) + 1;
				ok = sb_u32 (&b, 12 + nl + vl) && sb_u32 (&b, nl) && sb_u32 (&b, vl)
					&& sb_put (&b, mac->name, nl) && sb_put (&b, mac->value, vl);
			}
			if (ok)
				sb_set32 (&b, ls, b.n - ls);
		}
		ok = ok && sb_put (&b, p->rest, p->rest_size);
		if (ok)
			sb_set32 (&b, start, b.n - start);
	}
	if (ok)
		sb_set32 (&b, plist, b.n - plist);

	const uint slist = b.n;
	ok = ok && sb_u32 (&b, 0) && sb_u32 (&b, s->n_src);
	for (uint i = 0; ok && i < s->n_src; i++)
	{
		const sharc_src_t *sr = s->src + i;
		const uint nl = strlen (sr->name) + 1;
		ok = sb_u32 (&b, 16 + nl + sr->code_size) && sb_u32 (&b, nl)
			&& sb_u32 (&b, sr->code_size) && sb_u32 (&b, sr->code_size)
			&& sb_put (&b, sr->name, nl) && sb_put (&b, sr->code, sr->code_size);
	}
	if (ok)
	{
		sb_set32 (&b, slist, b.n - slist);
		sb_set32 (&b, 8, b.n);
	}
	if (!ok)
	{
		FREE (b.p);
		return ERR_CANT_CREATE;
	}
	*out = b.p;
	*out_size = b.n;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
// directory I/O

static enumError write_file (ccp dir, ccp rel, const void *data, uint size, ccp source)
{
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%s", dir, rel);
	enumError err = CreatePath (path, false);
	if (err)
		return err;
	File_t F;
	err = CreateFileOpt (&F, true, path, false, source ? source : path);
	if (F.f && size && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
	ResetFile (&F, opt_preserve);
	return err;
}

static bool safe_name (ccp n)
{
	return *n && strcmp (n, ".") && strcmp (n, "..") && !strpbrk (n, "/\\:\r\n\t");
}

static void manifest_text (const sharc_t *s, sbuf_t *m)
{
	char line[1024];
	int n = snprintf (line, sizeof (line), "SHARC %u %s\nname %s\n", s->version,
		s->be ? "big" : "little", s->name);
	sb_put (m, line, n);
	static const char kind[3] = { 'v', 'f', 'g' };
	for (uint i = 0; i < s->n_prog; i++)
	{
		const sharc_prog_t *p = s->prog + i;
		n = snprintf (line, sizeof (line), "program %s\nvtx %d\nfrg %d\ngeo %d\n", p->name,
			p->shader[0], p->shader[1], p->shader[2]);
		sb_put (m, line, n);
		for (uint k = 0; k < 3; k++)
			for (uint j = 0; j < p->n_macro[k]; j++)
			{
				sb_put (m, kind + k, 1);
				sb_put (m, "macro ", 6);
				sb_put (m, p->macro[k][j].name, strlen (p->macro[k][j].name));
				sb_put (m, "\t", 1);
				sb_put (m, p->macro[k][j].value, strlen (p->macro[k][j].value));
				sb_put (m, "\n", 1);
			}
		n = snprintf (line, sizeof (line), "rest programs/%03u.bin\n", i);
		sb_put (m, line, n);
	}
	for (uint i = 0; i < s->n_src; i++)
	{
		sb_put (m, "source ", 7);
		sb_put (m, s->src[i].name, strlen (s->src[i].name));
		sb_put (m, "\n", 1);
	}
}

enumError ExtractSHARCDir (const u8 *data, size_t size, ccp dest_dir, ccp source)
{
	sharc_t s;
	enumError err = sharc_parse (&s, data, size);
	if (err)
	{
		sharc_free (&s);
		return err == ERR_INVALID_DATA ? ERR_NOTHING_TO_DO : err;
	}

	// names must be usable as file names, and unique
	for (uint i = 0; i < s.n_src; i++)
	{
		bool bad = !safe_name (s.src[i].name);
		for (uint j = 0; !bad && j < i; j++)
			bad = !strcmp (s.src[i].name, s.src[j].name);
		if (bad)
		{
			sharc_free (&s);
			return ERR_NOTHING_TO_DO;
		}
	}
	for (uint i = 0; i < s.n_prog; i++)
		if (strpbrk (s.prog[i].name, "\r\n") || strpbrk (s.name, "\r\n"))
		{
			sharc_free (&s);
			return ERR_NOTHING_TO_DO;
		}

	// prove the manifest+files reproduce the input before writing anything
	u8 *check = 0;
	uint check_size = 0;
	err = sharc_build (&s, &check, &check_size);
	const bool same = !err && check_size == size && !memcmp (check, data, size);
	FREE (check);
	if (!same)
	{
		sharc_free (&s);
		return ERR_NOTHING_TO_DO;
	}

	sbuf_t m = { 0, 0, 0, false };
	manifest_text (&s, &m);
	err = write_file (dest_dir, "sharc.txt", m.p, m.n, source);
	FREE (m.p);
	for (uint i = 0; !err && i < s.n_prog; i++)
	{
		char rel[64];
		snprintf (rel, sizeof (rel), "programs/%03u.bin", i);
		err = write_file (dest_dir, rel, s.prog[i].rest, s.prog[i].rest_size, source);
	}
	for (uint i = 0; !err && i < s.n_src; i++)
	{
		char rel[PATH_MAX];
		snprintf (rel, sizeof (rel), "sources/%s", s.src[i].name);
		err = write_file (dest_dir, rel, s.src[i].code, s.src[i].code_size, source);
	}
	sharc_free (&s);
	return err;
}

static enumError load_rel (ccp dir, ccp rel, u8 **data, size_t *size)
{
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%s", dir, rel);
	return LoadFileAlloc (path, 0, 0, data, size, 0, 0, 0, false);
}

static char *add_str (char *s, ccp text) // replaces the string S
{
	FREE (s);
	return STRDUP (text);
}

// Reads sharc.txt, programs/ and sources/ back into S.
static enumError sharc_read_dir (sharc_t *s, ccp dir)
{
	memset (s, 0, sizeof (*s));
	u8 *txt = 0;
	size_t txt_size = 0;
	enumError err = load_rel (dir, "sharc.txt", &txt, &txt_size);
	if (err)
		return err;
	char *buf = MALLOC (txt_size + 1);
	memcpy (buf, txt, txt_size);
	buf[txt_size] = 0;
	FREE (txt);

	bool have_header = false;
	sharc_prog_t *cur = 0;
	char **rest_file = 0;
	uint cap_prog = 0, cap_src = 0;
	for (char *line = buf, *next; line && *line; line = next)
	{
		next = strchr (line, '\n');
		if (next)
			*next++ = 0;
		char *cr = line + strlen (line);
		if (cr > line && cr[-1] == '\r')
			cr[-1] = 0;
		if (!*line || *line == '#')
			continue;
		char *arg = strchr (line, ' ');
		if (arg)
			*arg++ = 0;
		else
			arg = line + strlen (line);

		if (!strcmp (line, "SHARC"))
		{
			char order[16] = "";
			if (sscanf (arg, "%u %15s", &s->version, order) != 2)
				err = ERR_SYNTAX;
			s->be = !strcmp (order, "big");
			have_header = true;
		}
		else if (!strcmp (line, "name"))
			s->name = add_str (s->name, arg);
		else if (!strcmp (line, "program"))
		{
			if (s->n_prog == cap_prog)
			{
				cap_prog = cap_prog ? cap_prog * 2 : 16;
				s->prog = REALLOC (s->prog, cap_prog * sizeof (*s->prog));
				rest_file = REALLOC (rest_file, cap_prog * sizeof (*rest_file));
			}
			cur = s->prog + s->n_prog;
			memset (cur, 0, sizeof (*cur));
			cur->shader[0] = cur->shader[1] = cur->shader[2] = -1;
			cur->name = STRDUP (arg);
			rest_file[s->n_prog++] = 0;
		}
		else if (!strcmp (line, "vtx") || !strcmp (line, "frg") || !strcmp (line, "geo"))
		{
			if (!cur)
				err = ERR_SYNTAX;
			else
				cur->shader[line[0] == 'v' ? 0 : line[0] == 'f' ? 1 : 2] = atoi (arg);
		}
		else if (strlen (line) == 6 && !strcmp (line + 1, "macro") && strchr ("vfg", line[0]))
		{
			char *tab = strchr (arg, '\t');
			if (!cur || !tab)
			{
				err = ERR_SYNTAX;
				continue;
			}
			*tab++ = 0;
			const uint k = line[0] == 'v' ? 0 : line[0] == 'f' ? 1 : 2;
			cur->macro[k] = REALLOC (cur->macro[k], (cur->n_macro[k] + 1) * sizeof (*cur->macro[k]));
			cur->macro[k][cur->n_macro[k]].name = STRDUP (arg);
			cur->macro[k][cur->n_macro[k]++].value = STRDUP (tab);
		}
		else if (!strcmp (line, "rest"))
		{
			if (!cur)
				err = ERR_SYNTAX;
			else
				rest_file[s->n_prog - 1] = STRDUP (arg);
		}
		else if (!strcmp (line, "source"))
		{
			if (s->n_src == cap_src)
			{
				cap_src = cap_src ? cap_src * 2 : 16;
				s->src = REALLOC (s->src, cap_src * sizeof (*s->src));
			}
			sharc_src_t *sr = s->src + s->n_src++;
			memset (sr, 0, sizeof (*sr));
			sr->name = STRDUP (arg);
			if (!safe_name (arg))
				err = ERR_SYNTAX;
		}
		else
			err = ERROR0 (ERR_SYNTAX, "SHARC manifest: unknown line '%s'\n", line);
	}
	FREE (buf);
	if (!have_header || !s->name || s->version < 10 || s->version > 12)
		err = err ? err : ERROR0 (ERR_SYNTAX, "SHARC manifest: missing or bad header\n");

	for (uint i = 0; !err && i < s->n_prog; i++)
	{
		sharc_prog_t *p = s->prog + i;
		for (uint k = 0; k < 3; k++)
			if (!p->macro[k])
				p->macro[k] = CALLOC (1, sizeof (*p->macro[k]));
		if (!rest_file[i] || strncmp (rest_file[i], "programs/", 9) || !safe_name (rest_file[i] + 9))
		{
			err = ERROR0 (ERR_SYNTAX, "SHARC manifest: program %s has no 'rest' file\n", p->name);
			break;
		}
		size_t rs = 0;
		err = load_rel (dir, rest_file[i], &p->rest, &rs);
		p->rest_size = rs;
	}
	for (uint i = 0; !err && i < s->n_src; i++)
	{
		char rel[PATH_MAX];
		snprintf (rel, sizeof (rel), "sources/%s", s->src[i].name);
		size_t cs = 0;
		err = load_rel (dir, rel, &s->src[i].code, &cs);
		s->src[i].code_size = cs;
	}
	if (rest_file)
	{
		for (uint i = 0; i < s->n_prog; i++)
			FREE (rest_file[i]);
		FREE (rest_file);
	}
	if (err)
		sharc_free (s);
	return err;
}

static enumError write_output (ccp dest, const u8 *data, uint size, ccp source)
{
	File_t F;
	enumError err = CreateFileOpt (&F, true, dest, testmode, source);
	if (!testmode && F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	return err;
}

enumError CreateSHARCFromDir (ccp source_dir, ccp dest)
{
	sharc_t s;
	enumError err = sharc_read_dir (&s, source_dir);
	if (err)
		return err;
	u8 *out = 0;
	uint out_size = 0;
	err = sharc_build (&s, &out, &out_size);
	if (!err)
		err = write_output (dest, out, out_size, dest);
	FREE (out);
	sharc_free (&s);
	return err;
}

bool LooksLikeSHARCDir (ccp dir)
{
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/sharc.txt", dir);
	struct stat st;
	return !stat (path, &st) && S_ISREG (st.st_mode);
}

///////////////////////////////////////////////////////////////////////////////
// Wii U GX2 shader header: big endian (as stored in GSH) -> little endian
// (as stored in SHARCFB). SharcCompiler's gx2shader.py, "align = False".

ccp opt_with_gshcompile = 0; // --with-gshcompile=path|name

typedef struct gx2r_t
{
	const u8 *h;
	uint size;
} gx2r_t;

static bool in_range (const gx2r_t *r, u32 off, u64 len)
{
	return off <= r->size && len <= r->size - off;
}

// GX2 header pointers carry a tag in their upper bits
#define PTR_MASK_TABLE 0xD0600000u
#define PTR_MASK_NAME 0xCA700000u

static ccp gx2_name (const gx2r_t *r, u32 raw)
{
	const u32 off = raw & ~PTR_MASK_NAME;
	if (off >= r->size || !memchr (r->h + off, 0, r->size - off))
		return 0;
	return (ccp)r->h + off;
}

typedef struct gx2_table_t
{
	uint count, entry;
	u32 off; // untagged offset into the header
} gx2_table_t;

static bool gx2_table (const gx2r_t *r, gx2_table_t *t, u32 count, u32 raw, uint entry)
{
	t->count = count;
	t->entry = entry;
	t->off = raw & ~PTR_MASK_TABLE;
	return !count || in_range (r, t->off, (u64)count * entry);
}

static enumError gx2_shader_to_fb (
	bool vertex, const u8 *h, uint hs, const u8 *code, uint code_size, uint bin_pos, sbuf_t *out)
{
	const gx2r_t R = { h, hs }, *r = &R;
	const uint nregs = vertex ? 52 : 41, regs_len = nregs * 4;
	const uint fixed = vertex ? 308 : 232;
	if (hs < fixed)
		return ERROR0 (ERR_INVALID_DATA, "GSH shader header is too short\n");

	const u8 *f = h + regs_len + 8;
	const u32 mode = rd32 (f, true);
	gx2_table_t ub, uv, loop, samp, att = { 0, 0, 0 };
	if (rd32 (f + 20, true))
		return ERROR0 (ERR_INVALID_DATA, "GSH shader initial values are not supported\n");
	if (!gx2_table (r, &ub, rd32 (f + 4, true), rd32 (f + 8, true), 12)
		|| !gx2_table (r, &uv, rd32 (f + 12, true), rd32 (f + 16, true), 20)
		|| !gx2_table (r, &loop, rd32 (f + 28, true), rd32 (f + 32, true), 8)
		|| !gx2_table (r, &samp, rd32 (f + 36, true), rd32 (f + 40, true), 12))
		return ERROR0 (ERR_INVALID_DATA, "GSH shader table is out of range\n");
	u32 ring = 0, has_so = 0;
	const u8 *so = 0, *rbuf;
	if (vertex)
	{
		if (!gx2_table (r, &att, rd32 (f + 44, true), rd32 (f + 48, true), 16))
			return ERROR0 (ERR_INVALID_DATA, "GSH shader table is out of range\n");
		ring = rd32 (f + 52, true);
		has_so = rd32 (f + 56, true);
		so = f + 60;
		rbuf = f + 76;
	}
	else
		rbuf = f + 44;

	// layout: header, ub, uv, samplers, [attribs], string table, loop vars
	uint off = fixed;
	const uint ub_off = ub.count ? off : 0;
	off += ub.count * 12;
	const uint uv_off = uv.count ? off : 0;
	off += uv.count * 20;
	const uint samp_off = samp.count ? off : 0;
	off += samp.count * 12;
	const uint att_off = att.count ? off : 0;
	off += att.count * 16;
	const uint str_off = off;

	// names, in table order, resolved against the source header
	typedef struct nm_t
	{
		ccp s;
		uint pos;
	} nm_t;
	const uint n_names = ub.count + uv.count + samp.count + att.count;
	nm_t *nm = CALLOC (n_names ? n_names : 1, sizeof (*nm));
	uint ni = 0, spos = 0;
	const gx2_table_t *order[4] = { &ub, &uv, &samp, &att };
	enumError err = ERR_OK;
	for (uint t = 0; !err && t < 4; t++)
		for (uint i = 0; i < order[t]->count; i++)
		{
			const ccp s = gx2_name (r, rd32 (h + order[t]->off + i * order[t]->entry, true));
			if (!s)
			{
				err = ERROR0 (ERR_INVALID_DATA, "GSH shader name is out of range\n");
				break;
			}
			nm[ni].s = s;
			nm[ni].pos = spos; // first entry with equal text wins in getPos()
			spos += strlen (s) + 1;
			ni++;
		}
	if (err)
	{
		FREE (nm);
		return err;
	}
	for (uint i = 0; i < n_names; i++)
		for (uint j = 0; j < i; j++)
			if (!strcmp (nm[i].s, nm[j].s))
			{
				nm[i].pos = nm[j].pos;
				break;
			}
	off += spos;
	const uint loop_off = loop.count ? off : 0;

	sbuf_t b = { 0, 0, 0, false };
	bool ok = true;
	for (uint i = 0; ok && i < nregs; i++)
		ok = sb_u32 (&b, rd32 (h + i * 4, true));
	ok = ok && sb_u32 (&b, code_size) && sb_u32 (&b, 0) && sb_u32 (&b, mode) && sb_u32 (&b, ub.count)
		&& sb_u32 (&b, ub_off) && sb_u32 (&b, uv.count) && sb_u32 (&b, uv_off) && sb_u32 (&b, 0)
		&& sb_u32 (&b, 0) && sb_u32 (&b, loop.count) && sb_u32 (&b, loop_off)
		&& sb_u32 (&b, samp.count) && sb_u32 (&b, samp_off);
	if (vertex)
	{
		ok = ok && sb_u32 (&b, att.count) && sb_u32 (&b, att_off) && sb_u32 (&b, ring)
			&& sb_u32 (&b, has_so != 0);
		for (uint i = 0; ok && i < 4; i++)
			ok = sb_u32 (&b, rd32 (so + i * 4, true));
	}
	ok = ok && sb_u32 (&b, rd32 (rbuf, true)) && sb_u32 (&b, rd32 (rbuf + 4, true))
		&& sb_u32 (&b, rd32 (rbuf + 8, true)) && sb_u32 (&b, 0);

	uint ni2 = 0;
	for (uint i = 0; ok && i < ub.count; i++, ni2++)
	{
		const u8 *e = h + ub.off + i * 12;
		ok = sb_u32 (&b, str_off + nm[ni2].pos) && sb_u32 (&b, rd32 (e + 4, true))
			&& sb_u32 (&b, rd32 (e + 8, true));
	}
	for (uint i = 0; ok && i < uv.count; i++, ni2++)
	{
		const u8 *e = h + uv.off + i * 20;
		ok = sb_u32 (&b, str_off + nm[ni2].pos);
		for (uint k = 1; ok && k < 5; k++)
			ok = sb_u32 (&b, rd32 (e + k * 4, true));
	}
	const uint samp_names = ni2;
	for (uint i = 0; ok && i < samp.count; i++)
	{
		const u8 *e = h + samp.off + i * 12;
		ok = sb_u32 (&b, str_off + nm[samp_names + i].pos) && sb_u32 (&b, rd32 (e + 4, true))
			&& sb_u32 (&b, rd32 (e + 8, true));
	}
	const uint att_names = samp_names + samp.count;
	for (uint i = 0; ok && i < att.count; i++)
	{
		const u8 *e = h + att.off + i * 16;
		ok = sb_u32 (&b, str_off + nm[att_names + i].pos);
		for (uint k = 1; ok && k < 4; k++)
			ok = sb_u32 (&b, rd32 (e + k * 4, true));
	}
	for (uint i = 0; ok && i < n_names; i++)
		ok = sb_put (&b, nm[i].s, strlen (nm[i].s) + 1);
	for (uint i = 0; ok && i < loop.count; i++)
		ok = sb_u32 (&b, rd32 (h + loop.off + i * 8, true))
			&& sb_u32 (&b, rd32 (h + loop.off + i * 8 + 4, true));
	FREE (nm);
	if (!ok)
	{
		FREE (b.p);
		return ERR_CANT_CREATE;
	}

	// pad so that the shader code starts on a 0x100 boundary of the file,
	// then record the padded header length as the code offset
	const uint end = bin_pos + b.n;
	const uint pad = (((end - 1) | 0xff) + 1) - end;
	if (b.n && pad)
	{
		u8 zeros[0x100] = { 0 };
		sb_put (&b, zeros, pad);
	}
	sb_set32 (&b, regs_len + 4, b.n);
	ok = sb_put (&b, code, code_size);
	if (ok)
		ok = sb_put (out, b.p, b.n);
	FREE (b.p);
	return ok ? ERR_OK : ERR_CANT_CREATE;
}

///////////////////////////////////////////////////////////////////////////////
// gshCompile invocation (optional; the Cafe SDK tool is not redistributable)

static int run_argv (char *const argv[])
{
#ifdef __MINGW32__
	const intptr_t rc = SpawnWaitQuoted (argv, true);
	return rc == -1 ? -1 : (int)rc;
#else
	const pid_t pid = fork ();
	if (pid < 0)
		return -1;
	if (!pid)
	{
		execvp (argv[0], argv);
		_Exit (127);
	}
	int status = 0;
	while (waitpid (pid, &status, 0) < 0 && errno == EINTR)
		;
	return WIFEXITED (status) ? WEXITSTATUS (status) : -1;
#endif
}

// SharcCompiler's replace_macros(): drop everything up to and including the
// "#version" line, prepend the AGL preamble, then override #define values.
static char *prepare_source (const sharc_src_t *sr, const sharc_macro_t *mac, uint n_mac,
	ccp type_define, uint *out_len, uint *replaced)
{
	sbuf_t b = { 0, 0, 0, false };
	static const char pre1[] = "#version 330\n#extension GL_ARB_texture_cube_map_array : enable\n"
							   "// ----- These macros are auto defined by AGL.-----\n";
	static const char pre2[] = "#define AGL_TARGET_GX2 \n"
							   "// ------------------------------------------------\n";
	sb_put (&b, pre1, sizeof (pre1) - 1);
	sb_put (&b, type_define, strlen (type_define));
	sb_put (&b, pre2, sizeof (pre2) - 1);

	const char *p = (ccp)sr->code, *end = p + sr->code_size;
	// skip up to the line containing "#version"
	for (const char *l = p; l < end;)
	{
		const char *nl = memchr (l, '\n', end - l);
		const char *le = nl ? nl + 1 : end;
		if (memmem (l, le - l, "#version", 8))
		{
			p = le;
			break;
		}
		l = le;
	}
	*replaced = 0;
	while (p < end)
	{
		const char *nl = memchr (p, '\n', end - p);
		const char *le = nl ? nl + 1 : end;
		bool done = false;
		if (le - p > 7 && !memcmp (p, "#define", 7))
		{
			const char *q = p + 7;
			while (q < le && (*q == ' ' || *q == '\t'))
				q++;
			const char *ne = q;
			while (ne < le && *ne != ' ' && *ne != '\t' && *ne != '\r' && *ne != '\n')
				ne++;
			for (uint i = 0; i < n_mac; i++)
				if (strlen (mac[i].name) == (uint)(ne - q) && !memcmp (mac[i].name, q, ne - q))
				{
					char line[512];
					int n = snprintf (line, sizeof (line), "#define %s %s\n", mac[i].name,
						mac[i].value);
					sb_put (&b, line, n);
					(*replaced)++;
					done = true;
					break;
				}
		}
		if (!done)
			sb_put (&b, p, le - p);
		p = le;
	}
	*out_len = b.n;
	return (char *)b.p;
}

static enumError compile_program (
	ccp dir, const sharc_t *s, const sharc_prog_t *p, char *gsh_path, uint gsh_size)
{
	if (!opt_with_gshcompile || !*opt_with_gshcompile)
		return ERROR0 (ERR_SEMANTIC,
			"No compiled shader found: %s\n"
			"  Put the gshCompile output there, or pass --with-gshcompile=<path to gshCompile>\n",
			gsh_path);
	if (p->shader[0] < 0 || p->shader[1] < 0 || p->shader[2] != -1 || (uint)p->shader[0] >= s->n_src
		|| (uint)p->shader[1] >= s->n_src)
		return ERROR0 (ERR_INVALID_DATA,
			"Program %s needs exactly a vertex and a fragment shader\n", p->name);

	const sharc_src_t *vs = s->src + p->shader[0], *fs = s->src + p->shader[1];
	char rel[PATH_MAX], vpath[PATH_MAX], fpath[PATH_MAX], hpath[PATH_MAX];
	enumError err = ERR_OK;
	for (int pass = 0; !err && pass < 2; pass++)
	{
		const sharc_src_t *sr = pass ? fs : vs;
		uint len, replaced;
		char *code = prepare_source (sr, p->macro[pass], p->n_macro[pass],
			pass ? "#define AGL_FRAGMENT_SHADER \n" : "#define AGL_VERTEX_SHADER \n", &len,
			&replaced);
		snprintf (rel, sizeof (rel), "%s/%s", p->name, sr->name);
		err = write_file (dir, rel, code, len, 0);
		FREE (code);
		snprintf (pass ? fpath : vpath, PATH_MAX, "%s/%s", dir, rel);
		if (verbose >= 0)
			fprintf (stdlog, "  %s: overrode %u macros in the %s shader\n", p->name, replaced,
				pass ? "fragment" : "vertex");
	}
	if (err)
		return err;
	snprintf (hpath, sizeof (hpath), "%s/%s/out.h", dir, p->name);

	char *argv1[] = { (char *)opt_with_gshcompile, "-p", fpath, "-v", vpath, "-o", gsh_path,
		"-no_limit_array_syms", "-nospark", 0 };
	char *argv2[] = { (char *)opt_with_gshcompile, "-p", fpath, "-v", vpath, "-oh", hpath,
		"-no_limit_array_syms", "-nospark", 0 };
	if (verbose >= 0)
		fprintf (stdlog, "  compiling %s with %s\n", p->name, opt_with_gshcompile);
	const int rc = run_argv (argv1);
	run_argv (argv2);
	struct stat st;
	if (stat (gsh_path, &st) || !S_ISREG (st.st_mode))
		return ERROR0 (ERR_INVALID_DATA, "gshCompile (exit %d) produced no output for program %s\n",
			rc, p->name);
	(void)gsh_size;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
// SHARCFB assembly

// Copies the SHARC program entry's symbol tables (variations, variation
// symbols, uniform variables/blocks, samplers, attributes) into OUT. For a
// v10 archive, which has no variation-symbol list, an empty one is inserted.
static enumError fb_copy_tables (const sharc_t *s, const sharc_prog_t *p, sbuf_t *out)
{
	const bool be = s->be;
	const u8 *r = p->rest, *end = p->rest + p->rest_size;
	uint lists = s->version == 10 ? 5 : 6;
	if (s->version == 12)
		return ERROR0 (ERR_INVALID_DATA, "SHARC version 12 can't be compiled to SHARCFB yet\n");
	for (uint i = 0; i < lists; i++)
	{
		uint cnt;
		const uint sz = list_extent (r, end, be, &cnt);
		if (!sz)
			return ERROR0 (ERR_INVALID_DATA, "Program %s: malformed symbol table\n", p->name);
		if (!sb_put (out, r, sz))
			return ERR_CANT_CREATE;
		r += sz;
		if (i == 0 && s->version == 10)
		{
			sb_u32 (out, 8);
			sb_u32 (out, 0);
		}
	}
	return ERR_OK;
}

enumError CreateSHARCFBFromDir (ccp source_dir, ccp dest)
{
	sharc_t s;
	enumError err = sharc_read_dir (&s, source_dir);
	if (err)
		return err;
	if (s.be || s.version == 12)
	{
		sharc_free (&s);
		return ERROR0 (ERR_INVALID_DATA,
			"Only little endian SHARC v10/v11 can be compiled to SHARCFB\n");
	}

	sbuf_t out = { 0, 0, 0, false };
	const uint name_len = strlen (s.name) + 1;
	sb_put (&out, "BAHS", 4);
	sb_u32 (&out, 8);
	sb_u32 (&out, 0);
	sb_u32 (&out, 1);
	sb_u32 (&out, 0);
	sb_u32 (&out, name_len);
	sb_str (&out, s.name);

	const uint blist = out.n;
	sb_u32 (&out, 0);
	sb_u32 (&out, s.n_prog * 2);
	for (uint i = 0; !err && i < s.n_prog; i++)
	{
		const sharc_prog_t *p = s.prog + i;
		if (!safe_name (p->name))
		{
			err = ERROR0 (ERR_INVALID_DATA, "Program name is not a usable directory name: %s\n",
				p->name);
			break;
		}
		char gsh[PATH_MAX];
		snprintf (gsh, sizeof (gsh), "%s/%s/out.gsh", source_dir, p->name);
		u8 *raw = 0;
		size_t raw_size = 0;
		struct stat gst;
		if (stat (gsh, &gst) || !S_ISREG (gst.st_mode))
		{
			err = compile_program (source_dir, &s, p, gsh, sizeof (gsh));
			if (err)
				break;
		}
		err = LoadFileAlloc (gsh, 0, 0, &raw, &raw_size, 0, 0, 0, false);
		if (err)
			break;
		gtx_t gtx;
		err = raw_size > 0x7fffffff ? ERR_INVALID_DATA : ScanGTX (&gtx, raw, raw_size);
		if (err)
		{
			FREE (raw);
			err = ERROR0 (ERR_INVALID_DATA, "Not a valid GSH file: %s\n", gsh);
			break;
		}
		const gtx_shader_t *sh[2] = { 0, 0 };
		for (uint k = 0; k < gtx.n_shaders; k++)
		{
			const gtx_shader_t *g = gtx.shaders + k;
			const int idx = g->stage == GTX_SHADER_VERTEX ? 0 : g->stage == GTX_SHADER_PIXEL ? 1 : -1;
			if (idx < 0 || !g->header || !g->program)
				continue;
			if (sh[idx])
				err = ERROR0 (ERR_INVALID_DATA, "GSH file has several shaders of one kind: %s\n", gsh);
			sh[idx] = g;
		}
		if (!err && (!sh[0] || !sh[1]))
			err = ERROR0 (ERR_INVALID_DATA, "GSH file needs a vertex and a pixel shader: %s\n", gsh);
		for (uint k = 0; !err && k < 2; k++)
		{
			const uint item = out.n;
			sb_u32 (&out, 0);
			sb_u32 (&out, k);
			sb_u32 (&out, 0);
			sb_u32 (&out, 0);
			const uint bin = out.n;
			err = gx2_shader_to_fb (k == 0, sh[k]->header->data, sh[k]->header->data_size,
				sh[k]->program->data, sh[k]->program->data_size, bin, &out);
			if (!err)
			{
				sb_set32 (&out, item, out.n - item);
				sb_set32 (&out, item + 12, out.n - bin);
			}
		}
		ResetGTX (&gtx);
		FREE (raw);
	}
	if (!err)
		sb_set32 (&out, blist, out.n - blist);

	const uint plist = out.n;
	sb_u32 (&out, 0);
	sb_u32 (&out, s.n_prog);
	for (uint i = 0; !err && i < s.n_prog; i++)
	{
		const sharc_prog_t *p = s.prog + i;
		const uint start = out.n;
		sb_u32 (&out, 0);
		sb_u32 (&out, strlen (p->name) + 1);
		sb_u32 (&out, 3);
		sb_u32 (&out, i * 2);
		sb_str (&out, p->name);
		err = fb_copy_tables (&s, p, &out);
		sb_set32 (&out, start, out.n - start);
	}
	if (!err)
	{
		sb_set32 (&out, plist, out.n - plist);
		sb_set32 (&out, 8, out.n);
		err = write_output (dest, out.p, out.n, dest);
	}
	FREE (out.p);
	sharc_free (&s);
	return err;
}
