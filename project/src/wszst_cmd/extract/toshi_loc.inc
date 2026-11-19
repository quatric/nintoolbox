// Blue Tongue Toshi terrain locators (the "Locators" / "LocatorVIS" symbols of Data/Terrain/<area>/*skel.trb, Nickelodeon Barnyard Wii; big endian), layout after OpenBarnyard (Source/Terrain/
// ATerrainLocator.h): Locators = {s32 count, ptr locators, u32 name count, ptr name table}; a locator is 32 bytes {float quaternion x y z w, float position x y z, u16 name id, u8 flags1 (low
// nibble * 0.2 = scale, high nibble = flags), s8 flags2}; LocatorVIS = {s32 section count, ptr sections}, a section {ptr name, u32 first locator, u32 count} (the visibility ranges of the
// locator list). Written as a YAML-style listing "<file>.yaml".
static float toshi_loc_f (const u8 *p)
{
	const u32 w = rd_be32 (p);
	float f;
	memcpy (&f, &w, 4);
	return f;
}

static ccp toshi_loc_str (const toshi_trb_t *t, u32 off)
{
	if (off >= t->sect_size)
		return 0;
	for (size_t i = off; i < t->sect_size; i++)
		if (!t->sect[i])
			return (ccp)t->sect + off;
	return 0;
}

static enumError extract_toshi_loc_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".trb"))
		return ERR_NOTHING_TO_DO;
	{
		FILE *pf = fopen (arg, "rb");
		if (!pf)
			return ERR_NOTHING_TO_DO;
		u8 magic[4];
		const bool hit = fread (magic, 1, 4, pf) == 4 && !memcmp (magic, "TSFB", 4);
		fclose (pf);
		if (!hit)
			return ERR_NOTHING_TO_DO;
	}
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	toshi_trb_t trb;
	if (OpenToshiTrb (&trb, raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const s64 lo = ToshiSymbol (&trb, "Locators"), vo = ToshiSymbol (&trb, "LocatorVIS");
	const u8 *s = trb.sect;
	const size_t sz = trb.sect_size;
	u32 n = 0, lp = 0, nn = 0, np = 0;
	if (lo >= 0 && (u64)lo + 16 <= sz)
	{
		n = rd_be32 (s + lo);
		lp = rd_be32 (s + lo + 4);
		nn = rd_be32 (s + lo + 8);
		np = rd_be32 (s + lo + 12);
	}
	if (lo >= 0 && n <= 1000000 && nn <= 1000000 && (u64)lp + 32ull * n <= sz && (u64)np + 4ull * nn <= sz)
	{
		bool good = true;
		for (u32 i = 0; i < n && good; i++)
			good = rd_be16 (s + lp + 32 * i + 28) < nn;
		for (u32 i = 0; i < nn && good; i++)
			good = toshi_loc_str (&trb, rd_be32 (s + np + 4 * i)) != 0;
		if (good)
		{
			char dest[PATH_MAX];
			if (opt_dest)
				SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".yaml", false);
			else
				snprintf (dest, sizeof (dest), "%s.yaml", arg);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT TOSHI-LOCATORS:%s (%u locators) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
			err = ERR_OK;
			File_t F;
			if (!testmode)
				err = CreateFileOpt (&F, true, dest, false, arg);
			if (!err && !testmode && F.f)
			{
				FILE *f = F.f;
				fprintf (f, "# Blue Tongue Toshi terrain locators, decoded from %s\nlocators: # %u entries\n", arg, n);
				for (u32 i = 0; i < n; i++)
				{
					const u8 *l = s + lp + 32 * i;
					const u32 id = rd_be16 (l + 28);
					fprintf (f, "  - name: \"%s\"\n    position: [%.9g, %.9g, %.9g]\n    rotation: [%.9g, %.9g, %.9g, %.9g]\n    scale: %.9g\n    flags: %u\n    flags2: %d\n",
						toshi_loc_str (&trb, rd_be32 (s + np + 4 * id)), toshi_loc_f (l + 16), toshi_loc_f (l + 20), toshi_loc_f (l + 24),
						toshi_loc_f (l), toshi_loc_f (l + 4), toshi_loc_f (l + 8), toshi_loc_f (l + 12), (l[30] & 15) * 0.2f, l[30] >> 4, (int)(s8)l[31]);
				}
				if (vo >= 0 && (u64)vo + 8 <= sz)
				{
					const u32 ns = rd_be32 (s + vo), sp = rd_be32 (s + vo + 4);
					if (ns <= 100000 && (u64)sp + 12ull * ns <= sz)
					{
						fprintf (f, "vis-sections: # %u entries\n", ns);
						for (u32 i = 0; i < ns; i++)
						{
							const u8 *e = s + sp + 12 * i;
							ccp nm = toshi_loc_str (&trb, rd_be32 (e));
							fprintf (f, "  - name: \"%s\"\n    first: %u\n    count: %u\n", nm ? nm : "", rd_be32 (e + 4), rd_be32 (e + 8));
						}
					}
				}
				if (ferror (f))
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing YAML failed: %s\n", dest);
			}
			if (!testmode)
				ResetFile (&F, opt_preserve);
		}
	}
	ResetToshiTrb (&trb);
	FREE (raw);
	return err;
}
