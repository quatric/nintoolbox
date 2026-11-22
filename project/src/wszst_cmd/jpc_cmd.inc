// JPC (JParticle effect container) tool, ported from GCFT (gclib/jpc.py, jpa.py).
// Supports JPAC1-00 and JPAC2-10. Particles are handled as opaque byte blobs, so every
// chunk (BEM1, BSP1, ESP1, ETX1, SSP1, FLD1, KFA1, TDB1) round-trips unchanged; only the
// particle id and the TDB1 texture id list are rewritten.
//
//   wszst JPC LIST    file.jpc
//   wszst JPC EXTRACT file.jpc dir          -> dir/XXXX.jpa  (particle + its TEX1 chunks)
//   wszst JPC IMPORT  file.jpc out.jpc a.jpa|dir ...  add/replace particles (+ textures)
//   wszst JPC SHOW    file.jpc [id]                    print emitter (BEM1) / base shape (BSP1) properties
//   wszst JPC SET     file.jpc out.jpc id=XXXX|all key=value...   edit those properties
//   wszst JPC DELETE  file.jpc out.jpc id...           remove particles
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


// ---- particle property editing (BEM1 emitter + BSP1 base shape), offsets from GCFT's jpa_chunks ----
enum { JK_INT, JK_FLOAT, JK_ROT, JK_COLOR };
typedef struct jf_t
{
	ccp key;
	char chunk[5];
	uint o100, o210; // offset of the container inside the chunk (0 = not in that version)
	u8 kind, w, shift, nbits; // container width in bytes, bit range (nbits 0 = whole container)
	bool sgn;
	ccp const *en; // enum names indexed by value, NULL-terminated
} jf_t;

static ccp const jn_vol[] = { "cube", "sphere", "cylinder", "torus", "point", "circle", "line", 0 };
static ccp const jn_shape[] = { "point", "line", "billboard", "direction", "direction_cross", "stripe",
	"stripe_cross", "rotation", "rotation_cross", "dir_billboard", "y_billboard", 0 };
static ccp const jn_bmode[] = { "none", "blend", "logic", 0 };
static ccp const jn_bfac[] = { "zero", "one", "src_color", "inv_src_color", "dst_color", "inv_dst_color",
	"src_alpha", "inv_src_alpha", "dst_alpha", "inv_dst_alpha", 0 };
static ccp const jn_cmp[] = { "never", "less", "lequal", "equal", "nequal", "gequal", "greater", "always", 0 };
static ccp const jn_aop[] = { "and", "or", "xor", "xnor", 0 };
static ccp const jn_bool[] = { "false", "true", 0 };

static const jf_t *jpc_fields (uint *n)
{
	static jf_t t[96];
	static uint c;
	if (!c)
	{
#define J(k, ch, a, b, kd, w, sh, nb, sg, en) t[c++] = (jf_t){ k, ch, a, b, kd, w, sh, nb, sg, en }
#define JF(k, a, b) J (k, "BEM1", a, b, JK_FLOAT, 4, 0, 0, 0, 0)
		JF ("rate", 0x1c, 0x4c); JF ("rate_random", 0x20, 0x50);
		JF ("volume_sweep", 0x10, 0x58); JF ("volume_min_rad", 0x14, 0x5c);
		J ("volume_size", "BEM1", 0x18, 0x74, JK_INT, 2, 0, 0, 0, 0);
		J ("div_number", "BEM1", 0x1a, 0x76, JK_INT, 2, 0, 0, 0, 0);
		J ("rate_step", "BEM1", 0x24, 0x78, JK_INT, 1, 0, 0, 0, 0);
		J ("max_frame", "BEM1", 0x26, 0x6e, JK_INT, 2, 0, 0, 1, 0);
		J ("start_frame", "BEM1", 0x28, 0x70, JK_INT, 2, 0, 0, 1, 0);
		J ("life_time", "BEM1", 0x2a, 0x72, JK_INT, 2, 0, 0, 1, 0);
		JF ("life_time_random", 0x2c, 0x54);
		JF ("init_vel_omni", 0x30, 0x34); JF ("init_vel_axis", 0x34, 0x38); JF ("init_vel_random", 0x38, 0x3c);
		JF ("init_vel_dir", 0x3c, 0x40); JF ("init_vel_ratio", 0x40, 0x48); JF ("spread", 0x44, 0x44);
		JF ("air_resist", 0x48, 0x60); JF ("moment", 0x50, 0x64);
		JF ("air_resist_random", 0x4c, 0); JF ("moment_random", 0x54, 0); JF ("accel", 0x58, 0); JF ("accel_random", 0x5c, 0);
		JF ("scale_x", 0x60, 0x10); JF ("scale_y", 0x64, 0x14); JF ("scale_z", 0x68, 0x18);
		JF ("trans_x", 0x6c, 0x1c); JF ("trans_y", 0x70, 0x20); JF ("trans_z", 0x74, 0x24);
		JF ("dir_x", 0x78, 0x28); JF ("dir_y", 0x7c, 0x2c); JF ("dir_z", 0x80, 0x30);
		J ("rot_x", "BEM1", 0x84, 0x68, JK_ROT, 2, 0, 0, 0, 0);
		J ("rot_y", "BEM1", 0x86, 0x6a, JK_ROT, 2, 0, 0, 0, 0);
		J ("rot_z", "BEM1", 0x88, 0x6c, JK_ROT, 2, 0, 0, 0, 0);
		J ("fixed_density", "BEM1", 0xc, 8, JK_INT, 4, 0, 1, 0, jn_bool);
		J ("fixed_interval", "BEM1", 0xc, 8, JK_INT, 4, 1, 1, 0, jn_bool);
		J ("inherit_scale", "BEM1", 0xc, 8, JK_INT, 4, 2, 1, 0, jn_bool);
		J ("follow_emitter", "BEM1", 0xc, 8, JK_INT, 4, 3, 1, 0, jn_bool);
		J ("follow_emitter_child", "BEM1", 0xc, 8, JK_INT, 4, 4, 1, 0, jn_bool);
		J ("volume_type", "BEM1", 0xc, 8, JK_INT, 4, 8, 3, 0, jn_vol);
		J ("shape_type", "BSP1", 0xc, 8, JK_INT, 4, 0, 4, 0, jn_shape);
		J ("dir_type", "BSP1", 0xc, 8, JK_INT, 4, 4, 3, 0, 0);
		J ("rot_type", "BSP1", 0xc, 8, JK_INT, 4, 7, 3, 0, 0);
		J ("base_plane_type", "BSP1", 0xc, 8, JK_INT, 4, 10, 1, 0, 0);
		J ("base_size_x", "BSP1", 0x14, 0x10, JK_FLOAT, 4, 0, 0, 0, 0);
		J ("base_size_y", "BSP1", 0x18, 0x14, JK_FLOAT, 4, 0, 0, 0, 0);
		J ("blend_mode", "BSP1", 0x1e, 0x18, JK_INT, 2, 0, 2, 0, jn_bmode);
		J ("blend_src", "BSP1", 0x1e, 0x18, JK_INT, 2, 2, 4, 0, jn_bfac);
		J ("blend_dst", "BSP1", 0x1e, 0x18, JK_INT, 2, 6, 4, 0, jn_bfac);
		J ("blend_logic", "BSP1", 0x1e, 0x18, JK_INT, 2, 10, 4, 0, 0);
		J ("alpha_comp0", "BSP1", 0x20, 0x1a, JK_INT, 1, 0, 3, 0, jn_cmp);
		J ("alpha_op", "BSP1", 0x20, 0x1a, JK_INT, 1, 3, 2, 0, jn_aop);
		J ("alpha_comp1", "BSP1", 0x20, 0x1a, JK_INT, 1, 5, 3, 0, jn_cmp);
		J ("alpha_ref0", "BSP1", 0x21, 0x1b, JK_INT, 1, 0, 0, 0, 0);
		J ("alpha_ref1", "BSP1", 0x22, 0x1c, JK_INT, 1, 0, 0, 0, 0);
		J ("depth_test", "BSP1", 0x23, 0x1d, JK_INT, 1, 0, 1, 0, jn_bool);
		J ("depth_func", "BSP1", 0x23, 0x1d, JK_INT, 1, 1, 3, 0, jn_cmp);
		J ("depth_write", "BSP1", 0x23, 0x1d, JK_INT, 1, 4, 1, 0, jn_bool);
		J ("z_compare", "BSP1", 0x23, 0x1d, JK_INT, 1, 5, 1, 0, jn_bool);
		J ("texture_index", "BSP1", 0x26, 0x20, JK_INT, 1, 0, 0, 0, 0);
		J ("prm_color", "BSP1", 0x2c, 0x26, JK_COLOR, 4, 0, 0, 0, 0);
		J ("env_color", "BSP1", 0x30, 0x2a, JK_COLOR, 4, 0, 0, 0, 0);
#undef JF
#undef J
	}
	*n = c;
	return t;
}

// Offset of a chunk inside a particle blob (0 = none); *csz receives its size.
static uint jpc_chunk_off (const jpc_part_t *p, int ver, ccp magic, uint *csz)
{
	const u8 *d = p->data;
	uint nch = ver ? jpc_r16 (d + 2) : jpc_r32 (d + 0xc);
	uint off = ver ? 8 : 0x20;
	for (uint i = 0; i < nch && off + 8 <= p->size; i++)
	{
		const u32 cs = jpc_r32 (d + off + 4);
		if (!memcmp (d + off, magic, 4))
			return *csz = cs, off;
		off += cs;
	}
	return 0;
}

static u8 *jpc_field_ptr (const jpc_part_t *p, int ver, const jf_t *f)
{
	const uint o = ver ? f->o210 : f->o100;
	uint cs;
	const uint co = o ? jpc_chunk_off (p, ver, f->chunk, &cs) : 0;
	return co && o + f->w <= cs && co + o + f->w <= p->size ? p->data + co + o : 0;
}

static u32 jpc_raw (const u8 *q, uint w)
{
	u32 v = 0;
	for (uint i = 0; i < w; i++)
		v = v << 8 | q[i];
	return v;
}

static void jpc_print_field (const jf_t *f, const u8 *q)
{
	u32 v = jpc_raw (q, f->w);
	if (f->kind == JK_FLOAT)
	{
		float x;
		memcpy (&x, &v, 4);
		printf ("%g", x);
	}
	else if (f->kind == JK_ROT)
		printf ("%g", (double)v * 360.0 / 65536.0);
	else if (f->kind == JK_COLOR)
		printf ("%08X", v);
	else
	{
		if (f->nbits)
			v = (v >> f->shift) & ((1u << f->nbits) - 1);
		uint k = 0;
		if (f->en)
			for (; f->en[k] && k < v; k++)
				;
		if (f->en && f->en[k] && k == v)
			fputs (f->en[k], stdout);
		else if (f->sgn)
			printf ("%d", f->w == 2 ? (int)(s16)v : (int)(s8)v);
		else
			printf ("%u", v);
	}
}

// Parse and store; returns nonzero on bad value.
static int jpc_set_field (const jf_t *f, u8 *q, ccp s)
{
	u32 raw;
	if (f->kind == JK_FLOAT)
	{
		char *e;
		const float x = strtof (s, &e);
		if (!*s || *e)
			return 1;
		memcpy (&raw, &x, 4);
	}
	else if (f->kind == JK_ROT)
	{
		char *e;
		const double x = strtod (s, &e);
		if (!*s || *e)
			return 1;
		raw = (u32)(long long)(x * 65536.0 / 360.0 + (x < 0 ? -0.5 : 0.5)) & 0xffff;
	}
	else if (f->kind == JK_COLOR)
	{
		char *e;
		const unsigned long long x = strtoull (s, &e, 16);
		if (!*s || *e || x > 0xffffffffull)
			return 1;
		raw = (u32)x;
	}
	else
	{
		long long x = -1;
		for (uint k = 0; f->en && f->en[k]; k++)
			if (!strcasecmp (s, f->en[k]))
				x = k;
		if (x < 0)
		{
			char *e;
			x = strtoll (s, &e, 0);
			if (!*s || *e)
				return 1;
		}
		const uint nb = f->nbits ? f->nbits : f->w * 8;
		if (f->sgn ? (x < -(1ll << (nb - 1)) || x >= (1ll << (nb - 1))) : (x < 0 || x >= (1ll << nb)))
			return 1;
		const u32 mask = nb >= 32 ? 0xffffffffu : (1u << nb) - 1;
		if (f->nbits)
			raw = (jpc_raw (q, f->w) & ~(mask << f->shift)) | (((u32)x & mask) << f->shift);
		else
			raw = (u32)x & (f->w == 4 ? 0xffffffffu : (1u << (f->w * 8)) - 1);
	}
	for (uint i = 0; i < f->w; i++)
		q[i] = (u8)(raw >> (8 * (f->w - 1 - i)));
	return 0;
}

static int jpc_parse_id (ccp s)
{
	char *e;
	const long v = strtol (s, &e, 16);
	return *s && !*e && v >= 0 && v <= 0xffff ? (int)v : -1;
}

static enumError jpc_cmd_show (const jpc_t *j, ParamList_t *rest)
{
	uint nf;
	const jf_t *tab = jpc_fields (&nf);
	const int only = rest ? jpc_parse_id (rest->arg) : -1;
	if (rest && only < 0)
		return ERROR0 (ERR_SYNTAX, "Not a valid hex particle ID: %s\n", rest->arg);
	uint shown = 0;
	for (uint i = 0; i < j->npart; i++)
	{
		const jpc_part_t *p = &j->part[i];
		if (only >= 0 && p->id != only)
			continue;
		shown++;
		printf ("\nparticle %04X\n", p->id);
		for (uint k = 0; k < nf; k++)
		{
			const u8 *q = jpc_field_ptr (p, j->ver, &tab[k]);
			if (!q)
				continue;
			printf ("  %-22s ", tab[k].key);
			jpc_print_field (&tab[k], q);
			putchar ('\n');
		}
	}
	return only >= 0 && !shown ? ERROR0 (ERR_SEMANTIC, "No such particle: %s\n", rest->arg) : ERR_OK;
}

// SET: rest = out.jpc id=XXXX|all key=value...
static enumError jpc_cmd_set (jpc_t *j, ParamList_t *rest)
{
	if (!rest || !rest->next)
		return ERROR0 (ERR_MISSING_PARAM, "Usage: wszst JPC SET in.jpc out.jpc id=XXXX|all key=value...\n");
	uint nf;
	const jf_t *tab = jpc_fields (&nf);
	int only = -2;
	for (ParamList_t *q = rest->next; q; q = q->next)
		if (!strncasecmp (q->arg, "id=", 3))
			only = !strcasecmp (q->arg + 3, "all") ? -1 : jpc_parse_id (q->arg + 3);
	if (only == -2 || only < -1)
		return ERROR0 (ERR_SYNTAX, "Need id=XXXX (hex) or id=all\n");
	uint writes = 0, matched = 0;
	for (uint i = 0; i < j->npart; i++)
		if (only < 0 || j->part[i].id == only)
			matched++;
	if (!matched)
		return ERROR0 (ERR_SEMANTIC, "No such particle\n");
	for (ParamList_t *q = rest->next; q; q = q->next)
	{
		char *eq = strchr (q->arg, '=');
		if (!eq || !strncasecmp (q->arg, "id=", 3))
			continue;
		const jf_t *f = 0;
		for (uint k = 0; k < nf; k++)
			if (strlen (tab[k].key) == (size_t)(eq - q->arg) && !strncasecmp (tab[k].key, q->arg, eq - q->arg))
				f = &tab[k];
		if (!f)
			return ERROR0 (ERR_SYNTAX, "Unknown JPC key: %.*s\n", (int)(eq - q->arg), q->arg);
		for (uint i = 0; i < j->npart; i++)
		{
			if (only >= 0 && j->part[i].id != only)
				continue;
			u8 *p = jpc_field_ptr (&j->part[i], j->ver, f);
			if (!p)
				return ERROR0 (ERR_SEMANTIC, "Key %s not available in particle %04X (this JPC version)\n", f->key,
					j->part[i].id);
			if (jpc_set_field (f, p, eq + 1))
				return ERROR0 (ERR_SEMANTIC, "Invalid value: %s\n", q->arg);
			writes++;
		}
	}
	u8 *o;
	size_t on;
	if (jpc_save (j, &o, &on))
		return ERROR0 (ERR_INTERNAL, "Internal error: missing texture\n");
	const int bad = jpc_write_file (rest->arg, o, on);
	FREE (o);
	if (bad)
		return ERROR1 (ERR_CANT_CREATE, "Can't write file: %s\n", rest->arg);
	printf ("JPC SET %s: %u value write(s)\n", rest->arg, writes);
	return ERR_OK;
}

// DELETE: rest = out.jpc id...  (textures are kept, as in GCFT)
static enumError jpc_cmd_delete (jpc_t *j, ParamList_t *rest)
{
	if (!rest || !rest->next)
		return ERROR0 (ERR_MISSING_PARAM, "Usage: wszst JPC DELETE in.jpc out.jpc id...\n");
	uint del = 0;
	for (ParamList_t *q = rest->next; q; q = q->next)
	{
		const int id = jpc_parse_id (q->arg);
		if (id < 0)
			return ERROR0 (ERR_SYNTAX, "Not a valid hex particle ID: %s\n", q->arg);
		uint i = 0;
		while (i < j->npart && j->part[i].id != id)
			i++;
		if (i == j->npart)
			return ERROR0 (ERR_SEMANTIC, "No such particle: %s\n", q->arg);
		FREE (j->part[i].data);
		FREE (j->part[i].tex_names);
		memmove (&j->part[i], &j->part[i + 1], (j->npart - i - 1) * sizeof (*j->part));
		j->npart--;
		del++;
	}
	u8 *o;
	size_t on;
	if (jpc_save (j, &o, &on))
		return ERROR0 (ERR_INTERNAL, "Internal error: missing texture\n");
	const int bad = jpc_write_file (rest->arg, o, on);
	FREE (o);
	if (bad)
		return ERROR1 (ERR_CANT_CREATE, "Can't write file: %s\n", rest->arg);
	printf ("JPC DELETE %s: %u particle(s) removed, %u left\n", rest->arg, del, j->npart);
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
	else if (!strcasecmp (sub, "SHOW"))
		err = jpc_cmd_show (&j, rest);
	else if (!strcasecmp (sub, "SET"))
		err = jpc_cmd_set (&j, rest);
	else if (!strcasecmp (sub, "DELETE"))
		err = jpc_cmd_delete (&j, rest);
	else
		err = ERROR0 (ERR_SYNTAX, "Unknown JPC action (use LIST, SHOW, EXTRACT, IMPORT, SET or DELETE): %s\n", sub);
	jpc_free (&j);
	return err;
}
