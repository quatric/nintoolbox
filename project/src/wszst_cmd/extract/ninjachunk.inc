// Sega Ninja chunk model ("NJCM", Radirgy Noa Wii; the Wii port stores the tags byte reversed, "MCJN" / "LTJN", and
// every field big-endian; chunk heads are {u8 flags, u8 type}). A .nj file is "LTJN" (NJTL texture list: {u32 size} then
// body {u32 array offset, u32 count}, entries {u32 name offset, attr, ptr}, names relative to the body), optional "0FOP"
// (POF0) and "MCJN" {u32 size} + object data. Object node (0x34 bytes): {u32 eval flags, u32 model, f32 pos[3], i32 ang[3]
// (BAMS, 65536 = 360 deg), f32 scl[3], u32 child, u32 sibling}, flags 1 = no position, 2 = no rotation, 4 = no scale,
// 0x20 = ZYX rotation order. Model: {u32 vertex list, u32 polygon list, f32 center[3], f32 radius}. Vertex chunks (types
// 32..50: {head, u16 size in u32, u16 index offset, u16 count, vertices}, 12 / 16 / 24 / 28 bytes per vertex, position
// first, normal next when the vertex is 24 or 28 bytes); polygon chunks: 1..5 bits, 8/9 tiny (texture id), 17..31 material
// (skipped), 64/65/66 strips {head, u16 size in u16, u16 count (14 bit) | user flags, strips of {s16 length, length x
// {u16 vertex, [s16 u, s16 v (65: /256, 66: /1023)], user flags}}}. Written as "model.obj" (all nodes in world space,
// one material per texture id) and "model.mtl" (texture ids mapped through the NJTL names to "<name>.png").
typedef struct njc_t
{
	const u8 *d;
	size_t size, base, body_end;
	float (*pos)[3], (*nrm)[3];
	uint *oidx;
	char **names;
	uint nnames;
	char *obj;
	size_t cap, used;
	uint nv, nvt, nvn, ntri, nnodes;
	int cur_tex;
	bool ok;
	uint tex_seen[256];
} njc_t;

static float njc_f (const u8 *p)
{
	const u32 v = rd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static void njc_emit (njc_t *c, const char *fmt, ...)
{
	va_list ap;
	for (;;)
	{
		va_start (ap, fmt);
		const int w = vsnprintf (c->obj + c->used, c->cap - c->used, fmt, ap);
		va_end (ap);
		if (w >= 0 && (size_t)w < c->cap - c->used)
		{
			c->used += (size_t)w;
			return;
		}
		char *nb = REALLOC (c->obj, c->cap * 2);
		if (!nb)
		{
			c->ok = false;
			return;
		}
		c->obj = nb;
		c->cap *= 2;
	}
}

static bool njc_in (const njc_t *c, size_t off, size_t n)
{
	return off <= c->body_end - c->base && n <= c->body_end - c->base - off;
}

typedef struct njc_mat_t
{
	double m[3][3], t[3];
} njc_mat_t;

static njc_mat_t njc_mul (const njc_mat_t *a, const njc_mat_t *b)
{
	njc_mat_t r;
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
			r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j];
		r.t[i] = a->m[i][0] * b->t[0] + a->m[i][1] * b->t[1] + a->m[i][2] * b->t[2] + a->t[i];
	}
	return r;
}

static njc_mat_t njc_rot (int axis, double a)
{
	njc_mat_t r = { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, { 0, 0, 0 } };
	const double c = cos (a), s = sin (a);
	const int i = (axis + 1) % 3, j = (axis + 2) % 3;
	r.m[i][i] = c;
	r.m[i][j] = -s;
	r.m[j][i] = s;
	r.m[j][j] = c;
	return r;
}

static void njc_node (njc_t *c, size_t off, const njc_mat_t *parent, uint depth)
{
	if (!c->ok || depth > 64 || c->nnodes > 20000 || !njc_in (c, off, 0x34))
		return;
	c->nnodes++;
	const u8 *n = c->d + c->base + off;
	const u32 flags = rd_be32 (n), model = rd_be32 (n + 4), child = rd_be32 (n + 0x2c), sib = rd_be32 (n + 0x30);
	njc_mat_t loc = { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, { 0, 0, 0 } };
	if (!(flags & 1))
		for (int i = 0; i < 3; i++)
			loc.t[i] = njc_f (n + 8 + 4 * i);
	if (!(flags & 2))
	{
		njc_mat_t r[3];
		for (int i = 0; i < 3; i++)
			r[i] = njc_rot (i, (double)(int)rd_be32 (n + 0x14 + 4 * i) * 3.14159265358979323846 / 32768.0);
		njc_mat_t q = flags & 0x20 ? njc_mul (&r[2], &r[1]) : njc_mul (&r[0], &r[1]);
		q = flags & 0x20 ? njc_mul (&q, &r[0]) : njc_mul (&q, &r[2]);
		loc = njc_mul (&loc, &q);
	}
	if (!(flags & 4))
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				loc.m[i][j] *= j == i ? 1.0 : 1.0, loc.m[i][j] *= njc_f (n + 0x20 + 4 * j);
	const njc_mat_t w = njc_mul (parent, &loc);
	if (model && njc_in (c, model, 8))
	{
		const u32 vl = rd_be32 (c->d + c->base + model), pl = rd_be32 (c->d + c->base + model + 4);
		for (size_t o = vl; vl && c->ok;)
		{
			if (!njc_in (c, o, 8))
			{
				c->ok = false;
				break;
			}
			const u8 *h = c->d + c->base + o;
			const uint type = h[1];
			if (type == 0xff)
				break;
			const uint sz = rd_be16 (h + 2);
			if (type < 32 || type > 50 || !njc_in (c, o, 4 + 4 * (size_t)sz) || sz < 1)
			{
				c->ok = false;
				break;
			}
			const uint idx = rd_be16 (h + 4), nb = rd_be16 (h + 6);
			if (nb && ((sz - 1) * 4) % nb == 0)
			{
				const uint vs = (sz - 1) * 4 / nb;
				for (uint i = 0; i < nb && vs >= 12 && idx + i < 65536; i++)
				{
					const u8 *v = h + 8 + (size_t)i * vs;
					double p[3] = { njc_f (v), njc_f (v + 4), njc_f (v + 8) };
					for (int k = 0; k < 3; k++)
						c->pos[idx + i][k] = (float)(w.m[k][0] * p[0] + w.m[k][1] * p[1] + w.m[k][2] * p[2] + w.t[k]);
					c->oidx[idx + i] = 0;
				}
			}
			o += 4 + 4 * (size_t)sz;
		}
		for (size_t o = pl; pl && c->ok;)
		{
			if (!njc_in (c, o, 4))
			{
				c->ok = false;
				break;
			}
			const u8 *h = c->d + c->base + o;
			const uint type = h[1];
			if (type == 0xff)
				break;
			if (type <= 5)
			{
				o += 2;
				continue;
			}
			if (type == 8 || type == 9)
			{
				c->cur_tex = rd_be16 (h + 2) & 0x1fff;
				o += 4;
				continue;
			}
			const uint sz = rd_be16 (h + 2);
			if (type >= 64 && type <= 66 && njc_in (c, o, 4 + 2 * (size_t)sz))
			{
				const u8 *s = h + 6, *end = h + 4 + 2 * (size_t)sz;
				const uint cnt = rd_be16 (h + 4) & 0x3fff, ul = rd_be16 (h + 4) >> 14;
				for (uint k = 0; k < cnt && s + 2 <= end; k++)
				{
					int len = (s16)rd_be16 (s);
					s += 2;
					const bool rev = len < 0;
					if (rev)
						len = -len;
					const uint per = 2 + (type == 64 ? 0 : 4);
					uint vi[512], ti[512];
					if (len > 512 || s + (size_t)len * per + (size_t)(ul * 2) * (len > 2 ? len - 2 : 0) > end)
					{
						c->ok = false;
						break;
					}
					for (int i = 0; i < len; i++)
					{
						const uint ix = rd_be16 (s);
						double u = 0, vv = 0;
						if (type != 64)
						{
							const double div = type == 65 ? 256.0 : 1023.0;
							u = (s16)rd_be16 (s + 2) / div;
							vv = (s16)rd_be16 (s + 4) / div;
						}
						s += per + (i >= 2 ? ul * 2 : 0);
						if (ix >= 65536)
						{
							c->ok = false;
							break;
						}
						if (!c->oidx[ix])
						{
							njc_emit (c, "v %g %g %g\n", c->pos[ix][0], c->pos[ix][1], c->pos[ix][2]);
							c->oidx[ix] = ++c->nv;
						}
						vi[i] = c->oidx[ix];
						njc_emit (c, "vt %g %g\n", u, 1.0 - vv);
						ti[i] = ++c->nvt;
					}
					if (!c->ok)
						break;
					if (c->cur_tex >= 0 && c->cur_tex < 256)
						c->tex_seen[c->cur_tex] = 1;
					njc_emit (c, "usemtl tex%d\n", c->cur_tex);
					for (int i = 2; i < len; i++)
					{
						const bool flip = ((i & 1) != 0) != rev;
						const int a = i - 2, b = flip ? i : i - 1, d2 = flip ? i - 1 : i;
						njc_emit (c, "f %u/%u %u/%u %u/%u\n", vi[a], ti[a], vi[b], ti[b], vi[d2], ti[d2]);
						c->ntri++;
					}
				}
			}
			else if (!(type >= 17 && type <= 31))
			{
				c->ok = false;
				break;
			}
			o += 4 + 2 * (size_t)sz;
		}
	}
	if (child)
		njc_node (c, child, &w, depth + 1);
	if (sib)
		njc_node (c, sib, parent, depth);
}

static enumError extract_ninjachunk_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".nj"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x20 || sz > 0x10000000
		|| (memcmp (raw, "LTJN", 4) && memcmp (raw, "MCJN", 4)))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	njc_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.size = sz;
	c.cur_tex = 0;
	c.ok = true;
	size_t p = 0, obj_base = 0, obj_end = 0;
	while (p + 8 <= sz)
	{
		const u32 tsz = rd_be32 (raw + p + 4);
		if (tsz > sz - p - 8)
			break;
		if (!memcmp (raw + p, "LTJN", 4) && tsz >= 8)
		{
			const u8 *b = raw + p + 8;
			const u32 arr = rd_be32 (b), cnt = rd_be32 (b + 4);
			if (cnt <= 256 && arr + 12 * (size_t)cnt <= tsz)
			{
				c.names = CALLOC (cnt + 1, sizeof (char *));
				for (uint i = 0; c.names && i < cnt; i++)
				{
					const u32 no = rd_be32 (b + arr + 12 * i);
					if (no < tsz)
					{
						c.names[i] = STRDUP ((ccp)b + no);
						c.nnames = i + 1;
					}
				}
			}
		}
		else if (!memcmp (raw + p, "MCJN", 4))
		{
			obj_base = p + 8;
			obj_end = obj_base + tsz;
			break;
		}
		p += 8 + tsz;
	}
	bool done = false;
	if (obj_base && obj_end <= sz)
	{
		c.base = obj_base;
		c.body_end = obj_end;
		c.pos = CALLOC (65536, sizeof (*c.pos));
		c.nrm = CALLOC (65536, sizeof (*c.nrm));
		c.oidx = CALLOC (65536, sizeof (uint));
		c.cap = 1 << 16;
		c.obj = MALLOC (c.cap);
		if (c.pos && c.nrm && c.oidx && c.obj)
		{
			const njc_mat_t id = { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, { 0, 0, 0 } };
			njc_node (&c, 0, &id, 0);
			done = c.ok && c.ntri;
		}
	}
	enumError err = done ? ERR_OK : ERR_NOTHING_TO_DO;
	if (done)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT NINJA:%s (%u nodes, %u triangles) -> %s/model.obj\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, c.nnodes, c.ntri, dest);
		if (!testmode)
		{
			size_t ml = 1024 + 128 * 256, mu = 0;
			char *mtl = MALLOC (ml);
			if (mtl)
			{
				for (uint i = 0; i < 256; i++)
					if (c.tex_seen[i])
					{
						mu += snprintf (mtl + mu, ml - mu, "newmtl tex%u\nKd 1 1 1\n", i);
						if (i < c.nnames && c.names[i])
							mu += snprintf (mtl + mu, ml - mu, "map_Kd %s.png\n", c.names[i]);
					}
				char *full = MALLOC (c.used + 32);
				if (full)
				{
					const int hl = snprintf (full, 32, "mtllib model.mtl\n");
					memcpy (full + hl, c.obj, c.used);
					err = shade_write (arg, dest, basedir, "model.obj", (u8 *)full, (uint)(c.used + hl));
					FREE (full);
				}
				if (!err)
					err = shade_write (arg, dest, basedir, "model.mtl", (u8 *)mtl, (uint)mu);
				FREE (mtl);
			}
		}
	}
	for (uint i = 0; i < c.nnames; i++)
		FREE (c.names[i]);
	FREE (c.names);
	FREE (c.pos);
	FREE (c.nrm);
	FREE (c.oidx);
	FREE (c.obj);
	FREE (raw);
	return err;
}
