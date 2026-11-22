// JPC (JParticle effect container) tool, ported from GCFT (gclib/jpc.py, jpa.py).
// Supports JPAC1-00 and JPAC2-10. Particles are handled as opaque byte blobs, so every
// chunk (BEM1, BSP1, ESP1, ETX1, SSP1, FLD1, KFA1, TDB1) round-trips unchanged; only the
// particle id and the TDB1 texture id list are rewritten.
//
//   wszst JPC LIST    file.jpc
//   wszst JPC EXTRACT file.jpc dir          -> dir/XXXX.jpa  (particle + its TEX1 chunks)
//   wszst JPC IMPORT  file.jpc out.jpc a.jpa|dir ...  add/replace particles (+ textures)
//
// A .jpa file is one particle followed by the TEX1 chunks it uses (as in GCFT). On import the
// particle id is taken from a 4-digit hex file name (XXXX.jpa), else from the .jpa header.

#include <sys/stat.h>
#include <dirent.h>

typedef struct jpc_tex_t
{
	u8 *data;
	u32 size;
	char name[0x15];
} jpc_tex_t;

typedef struct jpc_part_t
{
	u8 *data;
	u32 size;
	u16 id;
	uint ntex;
	char (*tex_names)[0x15]; // ntex names, resolved to ids on save
	uint tdb1_ids_off; // offset of the id list inside data, 0 if none
} jpc_part_t;

typedef struct jpc_t
{
	int ver; // 0: JPAC1-00, 1: JPAC2-10
	u8 head[0x20];
	uint head_size;
	jpc_part_t *part;
	uint npart;
	jpc_tex_t *tex;
	uint ntex;
} jpc_t;

static u32 jpc_r32 (const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u16 jpc_r16 (const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static void jpc_w16 (u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void jpc_w32 (u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

static void jpc_free (jpc_t *j)
{
	for (uint i = 0; i < j->npart; i++)
	{
		FREE (j->part[i].data);
		FREE (j->part[i].tex_names);
	}
	for (uint i = 0; i < j->ntex; i++)
		FREE (j->tex[i].data);
	FREE (j->part);
	FREE (j->tex);
	memset (j, 0, sizeof (*j));
}

// Parse one particle at d (avail bytes). Fills p (copying the data). Texture names are
// resolved by the caller. Returns 0 on success.
static int jpc_parse_particle (const u8 *d, size_t avail, int ver, jpc_part_t *p)
{
	const uint hs = ver ? 8 : 0x20;
	if (avail < hs)
		return 1;
	uint nchunks, ntex;
	if (ver)
	{
		p->id = jpc_r16 (d);
		nchunks = jpc_r16 (d + 2);
		ntex = d[6];
	}
	else
	{
		if (memcmp (d, "JEFFjpa1", 8))
			return 1;
		nchunks = jpc_r32 (d + 0xc);
		ntex = d[0x16];
		p->id = jpc_r16 (d + 0x18);
	}
	size_t off = hs;
	p->tdb1_ids_off = 0;
	for (uint i = 0; i < nchunks; i++)
	{
		if (off + 8 > avail)
			return 1;
		const u32 cs = jpc_r32 (d + off + 4);
		if (cs < 8 || off + cs > avail)
			return 1;
		if (!memcmp (d + off, "TDB1", 4))
		{
			const uint io = ver ? 8 : 0xc;
			if (io + 2 * (size_t)ntex > cs)
				return 1;
			p->tdb1_ids_off = (uint)off + io;
		}
		off += cs;
	}
	if (ntex && !p->tdb1_ids_off)
		return 1;
	p->size = (u32)off;
	p->ntex = ntex;
	p->data = MALLOC (off);
	memcpy (p->data, d, off);
	return 0;
}

static int jpc_parse_tex (const u8 *d, size_t avail, jpc_tex_t *t)
{
	if (avail < 0x40 || memcmp (d, "TEX1", 4))
		return 1;
	t->size = jpc_r32 (d + 4);
	if (t->size < 0x40 || t->size > avail)
		return 1;
	snprintf (t->name, sizeof (t->name), "%.20s", d + 0xc);
	t->data = MALLOC (t->size);
	memcpy (t->data, d, t->size);
	return 0;
}

static int jpc_load (const u8 *d, size_t sz, jpc_t *j)
{
	memset (j, 0, sizeof (*j));
	if (sz < 0x20 || memcmp (d, "JPAC", 4))
		return 1;
	if (!memcmp (d, "JPAC1-00", 8))
		j->ver = 0;
	else if (!memcmp (d, "JPAC2-10", 8))
		j->ver = 1;
	else
		return 1;
	j->head_size = j->ver ? 0x10 : 0x20;
	memcpy (j->head, d, 0x20);
	j->npart = jpc_r16 (d + 8);
	j->ntex = jpc_r16 (d + 0xa);
	j->part = CALLOC (j->npart ? j->npart : 1, sizeof (*j->part));
	j->tex = CALLOC (j->ntex ? j->ntex : 1, sizeof (*j->tex));

	size_t off = j->head_size;
	uint np = 0, nt = 0;
	for (; np < j->npart; np++)
	{
		if (jpc_parse_particle (d + off, sz - off, j->ver, &j->part[np]))
			goto fail;
		off += j->part[np].size;
		for (uint k = 0; k < np; k++)
			if (j->part[k].id == j->part[np].id)
				goto fail;
	}
	if (j->ver)
		off = jpc_r32 (d + 0xc);
	for (; nt < j->ntex; nt++)
	{
		if (off > sz || jpc_parse_tex (d + off, sz - off, &j->tex[nt]))
			goto fail;
		off += j->tex[nt].size;
	}
	for (uint i = 0; i < j->npart; i++)
	{
		jpc_part_t *p = &j->part[i];
		p->tex_names = CALLOC (p->ntex ? p->ntex : 1, sizeof (*p->tex_names));
		for (uint k = 0; k < p->ntex; k++)
		{
			const uint id = jpc_r16 (p->data + p->tdb1_ids_off + 2 * k);
			if (id >= j->ntex)
				goto fail;
			memcpy (p->tex_names[k], j->tex[id].name, sizeof (p->tex_names[k]));
		}
	}
	return 0;

fail:
	j->npart = np < j->npart ? np + 1 : np; // free partial data
	j->ntex = nt < j->ntex ? nt + 1 : nt;
	jpc_free (j);
	return 1;
}

static int jpc_find_tex (const jpc_t *j, const char *name)
{
	for (uint i = 0; i < j->ntex; i++)
		if (!strcmp (j->tex[i].name, name))
			return (int)i;
	return -1;
}

// Serialize into a new malloc'd buffer.
static int jpc_save (jpc_t *j, u8 **out, size_t *out_size)
{
	size_t total = j->head_size, ptot = 0, ttot = 0;
	for (uint i = 0; i < j->npart; i++)
		ptot += j->part[i].size;
	for (uint i = 0; i < j->ntex; i++)
		ttot += j->tex[i].size;
	total += ptot;
	total = (total + 0x1f) & ~(size_t)0x1f;
	const size_t tex_off = total;
	total += ttot;
	total = (total + 0x1f) & ~(size_t)0x1f;
	u8 *b = CALLOC (total, 1);
	memcpy (b, j->head, j->head_size);
	jpc_w16 (b + 8, (u16)j->npart);
	jpc_w16 (b + 0xa, (u16)j->ntex);
	if (j->ver)
		jpc_w32 (b + 0xc, (u32)tex_off);
	size_t off = j->head_size;
	for (uint i = 0; i < j->npart; i++)
	{
		jpc_part_t *p = &j->part[i];
		memcpy (b + off, p->data, p->size);
		for (uint k = 0; k < p->ntex; k++)
		{
			const int id = jpc_find_tex (j, p->tex_names[k]);
			if (id < 0)
			{
				FREE (b);
				return 1;
			}
			jpc_w16 (b + off + p->tdb1_ids_off + 2 * k, (u16)id);
		}
		off += p->size;
	}
	off = tex_off;
	for (uint i = 0; i < j->ntex; i++)
	{
		memcpy (b + off, j->tex[i].data, j->tex[i].size);
		off += j->tex[i].size;
	}
	*out = b;
	*out_size = total;
	return 0;
}

static int jpc_read_file (ccp path, u8 **d, size_t *sz)
{
	*d = 0;
	*sz = 0;
	return LoadFileAlloc (path, 0, 0, d, sz, 0, 0, 0, false) || !*d;
}

static int jpc_write_file (ccp path, const u8 *d, size_t n)
{
	FILE *f = fopen (path, "wb");
	if (!f)
		return 1;
	const int bad = fwrite (d, 1, n, f) != n;
	return fclose (f) || bad;
}

static void jpc_particle_to_jpa (const jpc_t *j, const jpc_part_t *p, u8 **out, size_t *n)
{
	size_t total = p->size;
	int ids[256];
	for (uint k = 0; k < p->ntex && k < 256; k++)
	{
		ids[k] = jpc_find_tex (j, p->tex_names[k]);
		total += j->tex[ids[k]].size;
	}
	u8 *b = MALLOC (total);
	memcpy (b, p->data, p->size);
	size_t off = p->size;
	for (uint k = 0; k < p->ntex && k < 256; k++)
	{
		memcpy (b + off, j->tex[ids[k]].data, j->tex[ids[k]].size);
		off += j->tex[ids[k]].size;
	}
	*out = b;
	*n = total;
}

static enumError jpc_cmd_list (const jpc_t *j, ccp path)
{
	printf ("\n%s: %s, %u particles, %u textures\n\n", path, j->ver ? "JPAC2-10" : "JPAC1-00",
		j->npart, j->ntex);
	printf ("id    size     textures\n----------------------------------------\n");
	for (uint i = 0; i < j->npart; i++)
	{
		const jpc_part_t *p = &j->part[i];
		printf ("%04X  %06x  ", p->id, p->size);
		for (uint k = 0; k < p->ntex; k++)
			printf ("%s%s", k ? ", " : "", p->tex_names[k]);
		putchar ('\n');
	}
	return ERR_OK;
}

static enumError jpc_cmd_extract (const jpc_t *j, ccp dir)
{
	if (mkdir (dir, 0777) && errno != EEXIST)
		return ERROR1 (ERR_CANT_CREATE_DIR, "Can't create directory: %s\n", dir);
	for (uint i = 0; i < j->npart; i++)
	{
		u8 *b;
		size_t n;
		jpc_particle_to_jpa (j, &j->part[i], &b, &n);
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%04X.jpa", dir, j->part[i].id);
		const int bad = jpc_write_file (path, b, n);
		FREE (b);
		if (bad)
			return ERROR1 (ERR_CANT_CREATE, "Can't write file: %s\n", path);
		if (verbose >= 0)
			printf ("EXTRACT %s\n", path);
	}
	return ERR_OK;
}

// Add or replace one particle (+ textures) from .jpa data.
static int jpc_import_jpa (jpc_t *j, const u8 *d, size_t sz, int force_id, uint *add, uint *rep)
{
	jpc_part_t np;
	memset (&np, 0, sizeof (np));
	if (jpc_parse_particle (d, sz, j->ver, &np))
		return 1;
	if (force_id >= 0)
	{
		np.id = (u16)force_id;
		if (j->ver)
			jpc_w16 (np.data, np.id);
		else
			jpc_w16 (np.data + 0x18, np.id);
	}
	np.tex_names = CALLOC (np.ntex ? np.ntex : 1, sizeof (*np.tex_names));
	size_t off = np.size;
	for (uint k = 0; k < np.ntex; k++)
	{
		jpc_tex_t t;
		memset (&t, 0, sizeof (t));
		if (off >= sz || jpc_parse_tex (d + off, sz - off, &t))
		{
			FREE (np.data);
			FREE (np.tex_names);
			return 1;
		}
		off += t.size;
		memcpy (np.tex_names[k], t.name, sizeof (t.name));
		const int ex = jpc_find_tex (j, t.name);
		if (ex >= 0)
		{
			FREE (j->tex[ex].data);
			j->tex[ex] = t;
		}
		else
		{
			j->tex = REALLOC (j->tex, (j->ntex + 1) * sizeof (*j->tex));
			j->tex[j->ntex++] = t;
		}
	}
	for (uint i = 0; i < j->npart; i++)
		if (j->part[i].id == np.id)
		{
			FREE (j->part[i].data);
			FREE (j->part[i].tex_names);
			j->part[i] = np;
			(*rep)++;
			return 0;
		}
	j->part = REALLOC (j->part, (j->npart + 1) * sizeof (*j->part));
	j->part[j->npart++] = np;
	(*add)++;
	return 0;
}

static int jpc_hex_id (ccp path)
{
	ccp b = strrchr (path, '/');
	b = b ? b + 1 : path;
	if (strlen (b) != 8 || strcasecmp (b + 4, ".jpa"))
		return -1;
	char *end;
	const long v = strtol (b, &end, 16);
	return end == b + 4 ? (int)v : -1;
}

static enumError jpc_import_path (jpc_t *j, ccp path, uint *add, uint *rep)
{
	struct stat st;
	if (stat (path, &st))
		return ERROR1 (ERR_CANT_OPEN, "Can't open: %s\n", path);
	if (S_ISDIR (st.st_mode))
	{
		DIR *dp = opendir (path);
		if (!dp)
			return ERROR1 (ERR_CANT_OPEN, "Can't open directory: %s\n", path);
		enumError err = ERR_OK;
		struct dirent *de;
		while (!err && (de = readdir (dp)))
		{
			const size_t l = strlen (de->d_name);
			if (l > 4 && !strcasecmp (de->d_name + l - 4, ".jpa"))
			{
				char sub[PATH_MAX];
				snprintf (sub, sizeof (sub), "%s/%s", path, de->d_name);
				err = jpc_import_path (j, sub, add, rep);
			}
		}
		closedir (dp);
		return err;
	}
	u8 *d;
	size_t sz;
	if (jpc_read_file (path, &d, &sz))
		return ERROR1 (ERR_CANT_OPEN, "Can't read: %s\n", path);
	const int bad = jpc_import_jpa (j, d, sz, jpc_hex_id (path), add, rep);
	FREE (d);
	if (bad)
		return ERROR0 (ERR_INVALID_DATA, "Invalid or mismatched-version JPA file: %s\n", path);
	return ERR_OK;
}

static enumError cmd_jpc ()
{
	ParamList_t *pa = first_param;
	if (!pa || !pa->next)
		return ERROR0 (ERR_MISSING_PARAM, "Usage: wszst JPC LIST|EXTRACT|IMPORT file.jpc ...\n");
	ccp sub = pa->arg;
	ccp path = pa->next->arg;
	ParamList_t *rest = pa->next->next;

	u8 *d;
	size_t sz;
	if (jpc_read_file (path, &d, &sz))
		return ERROR1 (ERR_CANT_OPEN, "Can't read: %s\n", path);
	jpc_t j;
	if (jpc_load (d, sz, &j))
	{
		FREE (d);
		return ERROR0 (ERR_INVALID_DATA, "Not a valid JPAC1-00 / JPAC2-10 file: %s\n", path);
	}
	FREE (d);

	enumError err = ERR_OK;
	if (!strcasecmp (sub, "LIST"))
		err = jpc_cmd_list (&j, path);
	else if (!strcasecmp (sub, "EXTRACT"))
		err = rest ? jpc_cmd_extract (&j, rest->arg)
				   : ERROR0 (ERR_MISSING_PARAM, "Missing destination directory!\n");
	else if (!strcasecmp (sub, "IMPORT"))
	{
		if (!rest || !rest->next)
			err = ERROR0 (ERR_MISSING_PARAM, "Usage: wszst JPC IMPORT in.jpc out.jpc a.jpa|dir...\n");
		else
		{
			ccp outp = rest->arg;
			uint add = 0, rep = 0;
			for (ParamList_t *q = rest->next; q && !err; q = q->next)
				err = jpc_import_path (&j, q->arg, &add, &rep);
			if (!err)
			{
				u8 *o;
				size_t on;
				if (jpc_save (&j, &o, &on))
					err = ERROR0 (ERR_INTERNAL, "Internal error: missing texture\n");
				else
				{
					if (jpc_write_file (outp, o, on))
						err = ERROR1 (ERR_CANT_CREATE, "Can't write file: %s\n", outp);
					else
						printf ("IMPORT %s: %u particles added, %u replaced, now %u particles, %u textures\n",
							outp, add, rep, j.npart, j.ntex);
					FREE (o);
				}
			}
		}
	}
	else
		err = ERROR0 (ERR_SYNTAX, "Unknown JPC action (use LIST, EXTRACT or IMPORT): %s\n", sub);
	jpc_free (&j);
	return err;
}
