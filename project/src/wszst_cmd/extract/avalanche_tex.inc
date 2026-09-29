// Avalanche .thb texture header + sibling .tbb pixels (Cars 2) -> PNG. One
// texture writes name.png; an atlas header writes name_0.png, name_1.png, ...
static enumError extract_avalanche_tex_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".thb"))
		return ERR_NOTHING_TO_DO;

	u8 *thb = 0;
	size_t thb_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &thb, &thb_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	const uint count = AvalancheTexCount (thb, thb_size);
	if (!count)
	{
		FREE (thb);
		return ERR_NOTHING_TO_DO;
	}
	char tbb_path[PATH_MAX];
	snprintf (tbb_path, sizeof (tbb_path), "%.*s.tbb", (int)(strlen (arg) - 4), arg);
	u8 *tbb = 0;
	size_t tbb_size = 0;
	err = LoadFileAlloc (tbb_path, 0, 0, &tbb, &tbb_size, 0, 0, 0, false);
	if (err)
	{
		FREE (thb);
		return ERR_NOTHING_TO_DO;
	}

	enumError result = ERR_OK;
	for (uint i = 0; i < count && !result; i++)
	{
		char ext[24], dest[PATH_MAX];
		if (count == 1)
			snprintf (ext, sizeof (ext), ".png");
		else
			snprintf (ext, sizeof (ext), "_%u.png", i);
		beside_source_dest_ext (dest, sizeof (dest), arg, ext);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT AVALANCHE-TEX:%s -> PNG:%s\n", arg, dest);
			continue;
		}
		if (blitz_output_current (arg, dest))
			continue;
		u8 *rgba = 0;
		uint w = 0, h = 0;
		err = DecodeAvalancheTex (&rgba, &w, &h, thb, thb_size, tbb, tbb_size, i);
		if (err)
		{
			// one undecodable entry must not stop the rest of the atlas
			if (err != ERR_NOTHING_TO_DO)
				result = err;
			continue;
		}
		err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, dest, 0, true);
		if (err)
			result = err;
		else if (verbose >= 0)
			fprintf (stdlog, "%sEXTRACT AVALANCHE-TEX:%s -> PNG:%s\n", verbose > 0 ? "\n" : "", arg,
				dest);
	}
	FREE (thb);
	FREE (tbb);
	return result;
}
