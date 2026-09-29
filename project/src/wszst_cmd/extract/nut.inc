static enumError extract_nut_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".nut") && !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || (!IsNUT (raw, raw_size)))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nut_t nut;
	err = ScanNUT (&nut, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NUT (Smash 4):%s (%u textures) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nut.n_textures, dest);

	for (uint i = 0; !err && i < nut.n_textures; i++)
	{
		if (testmode)
			continue;

		u8 *tex_data = 0;
		size_t tex_sz = 0;
		char ext[16] = ".bin";
		enumError terr = ExtractNUTTexture (&nut, i, &tex_data, &tex_sz, ext, sizeof (ext));
		if (terr || !tex_data || tex_sz == 0)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s%s", dest, basedir ? basedir : "",
			nut.textures[i].name, ext);

		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (tex_data, 1, tex_sz, F.f) != tex_sz)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", tex_sz, path);
		ResetFile (&F, opt_preserve);
		FREE (tex_data);
	}

	ResetNUT (&nut);
	FREE (raw);

	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
