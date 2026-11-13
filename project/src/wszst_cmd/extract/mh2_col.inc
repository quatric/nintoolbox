// Rockstar Leeds collision library (Manhunt 2 "collisions_wii.col", big endian): u32 entry count, then named entries. Each entry starts with a NUL-terminated name padded with 'p'
// bytes to 4 bytes, followed by a variable header (shape parameters) and, for mesh shapes, {u32 vertex count, 3 x f32 per vertex, u32 triangle count, 3 x u32 per triangle}. The
// header length varies, so the mesh block is located by checking that the counts, float sanity and triangle indices are consistent. Entries with meshes are written as groups of
// "<name>.col.d/<name>.obj"; entries that are only primitive shapes (boxes, spheres) are skipped.
static bool mh2col_name (const u8 *d, size_t n, size_t p, size_t *nl, size_t *next)
{
	size_t l = 0;
	while (p + l < n && l < 80 && ((d[p + l] >= 'A' && d[p + l] <= 'Z') || (d[p + l] >= 'a' && d[p + l] <= 'z') || (d[p + l] >= '0' && d[p + l] <= '9') || strchr ("_()-. ", d[p + l]) != 0) && d[p + l])
		l++;
	if (l < 2 || p + l >= n || d[p + l])
		return false;
	const size_t q = p + ((l + 1 + 3) & ~(size_t)3);
	if (q > n)
		return false;
	for (size_t i = p + l + 1; i < q; i++)
		if (d[i] != 'p')
			return false;
	*nl = l;
	*next = q;
	return true;
}

static bool mh2col_mesh (const u8 *d, size_t n, size_t q, size_t *vo, uint *nv, size_t *to, uint *nt, size_t *end)
{
	for (size_t o = q; o < q + 0x90 && o + 8 <= n; o += 4)
	{
		const uint v = rd_be32 (d + o);
		if (v < 3 || v > 3000)
			continue;
		const size_t e = o + 4 + 12 * (size_t)v;
		if (e + 4 > n)
			continue;
		bool sane = true;
		for (uint i = 0; i < 3 * v && sane; i++)
		{
			const uint w = rd_be32 (d + o + 4 + 4 * i);
			float f;
			memcpy (&f, &w, 4);
			sane = f > -1e5f && f < 1e5f;
		}
		if (!sane)
			continue;
		const uint t = rd_be32 (d + e);
		if (t < 1 || t > 6000 || e + 4 + 12 * (size_t)t > n)
			continue;
		bool idx = true;
		for (uint i = 0; i < 3 * t && idx; i++)
			idx = rd_be32 (d + e + 4 + 4 * i) < v;
		if (!idx)
			continue;
		*vo = o + 4;
		*nv = v;
		*to = e + 4;
		*nt = t;
		*end = e + 4 + 12 * (size_t)t;
		return true;
	}
	return false;
}

static enumError extract_mh2_col_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".col"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 16)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = rd_be32 (c);
	size_t nl, q;
	if (!count || count > 100000 || !mh2col_name (c, cs, 4, &nl, &q))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	uint nm = 0, ntri = 0, base = 0;
	for (size_t p = 4; p < cs;)
	{
		if (!mh2col_name (c, cs, p, &nl, &q))
		{
			p += 4;
			continue;
		}
		size_t vo, to, end;
		uint nv, nt;
		if (mh2col_mesh (c, cs, q, &vo, &nv, &to, &nt, &end))
		{
			sumo_printf (&b, "g %.*s\n", (int)nl, (ccp)c + p);
			for (uint i = 0; i < nv; i++)
			{
				float f[3];
				for (uint k = 0; k < 3; k++)
				{
					const uint w = rd_be32 (c + vo + 12 * i + 4 * k);
					memcpy (&f[k], &w, 4);
				}
				sumo_printf (&b, "v %g %g %g\n", f[0], f[1], f[2]);
			}
			for (uint i = 0; i < nt; i++)
				sumo_printf (&b, "f %u %u %u\n", rd_be32 (c + to + 12 * i) + 1 + base, rd_be32 (c + to + 12 * i + 4) + 1 + base, rd_be32 (c + to + 12 * i + 8) + 1 + base);
			base += nv;
			ntri += nt;
			nm++;
			p = end;
		}
		else
			p = q;
	}
	FREE (c);
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && nm && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".obj");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MH2-COL:%s (%u meshes, %u triangles) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nm, ntri, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
