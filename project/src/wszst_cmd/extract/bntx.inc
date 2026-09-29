static enumError extract_bntx_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bntx"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err || raw_size > UINT_MAX || raw_size < 0x40 || memcmp (raw, "BNTX", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	bntx_t bntx;
	err = ScanBNTX (&bntx, raw, (uint)raw_size);
	if (err || !bntx.n_textures)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// For single-texture BNTX files without an explicit destination directory,
	// let decode_wimg_file convert it beside the source without creating a directory.
	if (bntx.n_textures <= 1 && (!opt_dest || !*opt_dest))
	{
		ResetBNTX (&bntx);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest_dir[PATH_MAX];
	if (opt_dest && *opt_dest)
		snprintf (dest_dir, sizeof (dest_dir), "%s", opt_dest);
	else
		SubstDest (dest_dir, sizeof (dest_dir), arg, 0, "%P/%N.d", ".d", false);

	if (!testmode)
		CreatePath (dest_dir, true);

	for (uint i = 0; i < bntx.n_textures; i++)
	{
		ccp tname = bntx.textures[i].name;
		if (!tname || !*tname)
			tname = "texture";

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s.png", dest_dir, tname);

		if (!opt_overwrite && !access (path, F_OK))
			continue;

		u8 *rgba = 0;
		uint w = 0, h = 0;
		if (!DecodeBNTX_RGBA (&rgba, &w, &h, &bntx, i) && rgba)
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT BNTX:%s[%s] -> PNG:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", arg, tname, path);
			if (!testmode)
				SaveDecodedRGBAToPNG (rgba, w, h, &le_func, path, 0, false);
			else
				FREE (rgba);
		}
	}

	ResetBNTX (&bntx);
	FREE (raw);
	return ERR_OK;
}
