// Rockstar Leeds collision library (Manhunt 2 "collisions_wii.col", big endian; layout as documented by the Manhunt modding community and confirmed here by every file parsing exactly
// to its end): u32 entry count, then per entry a NUL-terminated name padded with 'p' bytes to 4 bytes, bounding sphere centre (3 x f32) and radius (f32), bounding box min and max
// (3 x f32 each), u32 sphere count with {centre 3 x f32, radius f32, 4 surface bytes}, u32 line count with 2 x 3 x f32 each, u32 box count (0 on the Wii files), u32 vertex count
// with 3 x f32 each and u32 triangle count with 3 x u32 indices each. Written as "<name>.obj": one group per entry with its vertices and triangles; spheres are kept as
// "# sphere x y z r material" comments and lines as "l" elements.
static float mh2col_f (const u8 *p)
{
	const uint v = rd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
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
	if (!count || count > 100000)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	uint nmesh = 0, ntri = 0, base = 0, parsed = 0;
	size_t p = 4;
	bool good = true;
	for (uint e = 0; e < count && good; e++)
	{
		size_t l = 0;
		while (p + l < cs && l < 96 && c[p + l])
			l++;
		if (!l || p + l >= cs)
		{
			good = false;
			break;
		}
		const size_t name = p;
		p += (l + 1 + 3) & ~(size_t)3;
		if (p + 40 + 4 > cs)
		{
			good = false;
			break;
		}
		p += 40;
		const uint ns = rd_be32 (c + p);
		p += 4;
		if (ns > 100000 || p + 20 * (size_t)ns + 4 > cs)
		{
			good = false;
			break;
		}
		const size_t spheres = p;
		p += 20 * (size_t)ns;
		const uint nl = rd_be32 (c + p);
		p += 4;
		if (nl > 100000 || p + 24 * (size_t)nl + 4 > cs)
		{
			good = false;
			break;
		}
		const size_t lines = p;
		p += 24 * (size_t)nl;
		const uint nb = rd_be32 (c + p);
		p += 4;
		if (nb || p + 4 > cs)
		{
			good = false;
			break;
		}
		const uint nv = rd_be32 (c + p);
		p += 4;
		if (nv > 100000 || p + 12 * (size_t)nv + 4 > cs)
		{
			good = false;
			break;
		}
		const size_t verts = p;
		p += 12 * (size_t)nv;
		const uint nt = rd_be32 (c + p);
		p += 4;
		if (nt > 200000 || p + 12 * (size_t)nt > cs)
		{
			good = false;
			break;
		}
		const size_t tris = p;
		p += 12 * (size_t)nt;
		parsed++;
		if (!nv && !ns && !nl)
			continue;
		sumo_printf (&b, "g %s\n", (ccp)c + name);
		for (uint i = 0; i < ns; i++)
			sumo_printf (&b, "# sphere %g %g %g %g %u\n", mh2col_f (c + spheres + 20 * i), mh2col_f (c + spheres + 20 * i + 4), mh2col_f (c + spheres + 20 * i + 8), mh2col_f (c + spheres + 20 * i + 12), c[spheres + 20 * i + 16]);
		for (uint i = 0; i < nv; i++)
			sumo_printf (&b, "v %g %g %g\n", mh2col_f (c + verts + 12 * i), mh2col_f (c + verts + 12 * i + 4), mh2col_f (c + verts + 12 * i + 8));
		for (uint i = 0; i < nt; i++)
		{
			const uint a = rd_be32 (c + tris + 12 * i), bb = rd_be32 (c + tris + 12 * i + 4), cc = rd_be32 (c + tris + 12 * i + 8);
			if (a < nv && bb < nv && cc < nv)
			{
				sumo_printf (&b, "f %u %u %u\n", a + 1 + base, bb + 1 + base, cc + 1 + base);
				ntri++;
			}
		}
		base += nv;
		for (uint i = 0; i < nl; i++)
		{
			sumo_printf (&b, "v %g %g %g\nv %g %g %g\nl %u %u\n", mh2col_f (c + lines + 24 * i), mh2col_f (c + lines + 24 * i + 4), mh2col_f (c + lines + 24 * i + 8), mh2col_f (c + lines + 24 * i + 12), mh2col_f (c + lines + 24 * i + 16), mh2col_f (c + lines + 24 * i + 20), base + 1, base + 2);
			base += 2;
		}
		nmesh++;
	}
	FREE (c);
	enumError err = ERR_NOTHING_TO_DO;
	if (good && p == cs && b.ok && nmesh && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], oname[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (oname, sizeof (oname), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (oname, '.');
		if (dot)
			strcpy (dot, ".obj");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MH2-COL:%s (%u of %u entries, %u triangles) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nmesh, parsed, ntri, dest, oname);
		err = shade_write (arg, dest, basedir, oname, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
