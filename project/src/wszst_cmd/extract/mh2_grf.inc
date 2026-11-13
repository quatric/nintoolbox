// Rockstar Leeds AI navigation graphs (Manhunt 2 "mapai_wii.grf", big endian): "AING", u32 version (1), u32 node count, then node records. A record is a NUL-terminated name padded
// with 'p' bytes to 4 bytes, a u32 layer, the position (3 x f32), a radius (f32), a tag string (same padding) and the link lists (not decoded; the records have no fixed size). The
// node starts are found by pattern and checked against the header count (matched exactly on 15 of the 16 level files). Written as "<name>.csv" (index, name, layer, x, y, z, radius).
static bool mh2grf_good (float v)
{
	return v == 0 || ((v > 1e-6f && v < 5000.0f) || (v < -1e-6f && v > -5000.0f));
}

static enumError extract_mh2_grf_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".grf"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 40 || memcmp (c, "AING", 4))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = rd_be32 (c + 8);
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "index,name,layer,x,y,z,radius\n");
	uint n = 0;
	for (size_t p = 12; p + 24 <= cs;)
	{
		size_t l = 0;
		while (p + l < cs && l < 40 && c[p + l] && (isalnum (c[p + l]) || strchr ("_-(). ", c[p + l])))
			l++;
		bool hit = false;
		if (l && p + l < cs && !c[p + l])
		{
			const size_t q = p + ((l + 1 + 3) & ~(size_t)3);
			bool pad = q + 20 <= cs;
			for (size_t i = p + l + 1; i < q && pad; i++)
				pad = c[i] == 'p';
			if (pad && rd_be32 (c + q) < 64)
			{
				float f[4];
				for (uint k = 0; k < 4; k++)
				{
					const uint w = rd_be32 (c + q + 4 + 4 * k);
					memcpy (&f[k], &w, 4);
				}
				if (mh2grf_good (f[0]) && mh2grf_good (f[1]) && mh2grf_good (f[2]) && f[3] > 0 && f[3] < 100)
				{
					sumo_printf (&b, "%u,%.*s,%u,%g,%g,%g,%g\n", n, (int)l, (ccp)c + p, rd_be32 (c + q), f[0], f[1], f[2], f[3]);
					n++;
					p = q + 20;
					hit = true;
				}
			}
		}
		if (!hit)
			p += 4;
	}
	FREE (c);
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && n && n + 8 >= count && n <= count && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".csv");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MH2-GRF:%s (%u of %u nodes) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, count, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
