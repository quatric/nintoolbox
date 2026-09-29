// Blue Tongue Toshi texture library (.ttl, Nickelodeon Barnyard Wii) -> PNGs
// in a directory named after the library.
static enumError extract_toshi_ttl_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".ttl"))
		return ERR_NOTHING_TO_DO;
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
	// trb.symb points into 'raw', so it must outlive every ToshiSymbol() lookup
	const uint count = ToshiTtlCount (&trb);
	if (!count)
	{
		ResetToshiTrb (&trb);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT TOSHI-TTL:%s (%u textures) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);

	char (*used)[128] = CALLOC (count, sizeof (*used));
	enumError err = used ? ERR_OK : ERR_OUT_OF_MEMORY;
	for (uint i = 0; i < count && !err && !testmode; i++)
	{
		u8 *rgba = 0;
		uint w = 0, h = 0;
		ccp tex_name = 0;
		if (DecodeToshiTexture (&trb, i, &tex_name, &rgba, &w, &h))
			continue;
		// "Particles\\smoke_2.tga" -> "Particles_smoke_2"
		char base[128];
		snprintf (base, sizeof (base), "%.120s", tex_name);
		char *dot = strrchr (base, '.');
		if (dot && strlen (dot) <= 5)
			*dot = 0;
		for (char *c = base; *c; c++)
			if (*c == '\\' || *c == '/' || *c == ':')
				*c = '_';
		if (!*base)
			snprintf (base, sizeof (base), "%04u", i);
		char name[160];
		for (uint dup = 0;; dup++)
		{
			snprintf (name, sizeof (name), dup ? "%s_%u" : "%s", base, dup);
			bool clash = false;
			for (uint j = 0; j < i && !clash; j++)
				clash = !strcasecmp (used[j], name);
			if (!clash)
				break;
		}
		snprintf (used[i], sizeof (used[i]), "%s", name);
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s.png", dest, basedir ? basedir : "", name);
		err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true);
	}
	FREE (used);
	ResetToshiTrb (&trb);
	FREE (raw);
	return err;
}
