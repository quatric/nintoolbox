// Blue Tongue "TRB\0" package (de Blob 2, Wii): "ttex" textures -> PNG in
// textures/, an embedded XUR section -> NAME.xur.
static enumError extract_bttrb_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".trb"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	uint count = 0;
	u32 xur_size = 0;
	const bool is_trb = IsBlueTongueTrb (raw, raw_size);
	bttrb_tex_t *list = is_trb ? ListBlueTongueTextures (raw, raw_size, &count) : 0;
	const u8 *xur = is_trb ? FindBlueTongueSection (raw, raw_size, "XUR", &xur_size) : 0;
	if (!count && !xur && !(is_trb && CountBlueTongueModels (raw, raw_size)))
	{
		FREE (list);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BLUETONGUE-TRB:%s (%u textures) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, dest);

	enumError err = ERR_OK;
	for (uint i = 0; i < count && !err && !testmode; i++)
	{
		u8 *rgba = 0;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%stextures/%s.png", dest, basedir ? basedir : "",
			list[i].name);
		if (!DecodeBlueTongueTexture (&rgba, raw, raw_size, list + i))
			err = SaveDecodedRGBAToPNG (
				rgba, list[i].width, list[i].height, &be_func, path, 0, true);
	}
	if (xur && !err && !testmode)
	{
		ccp base = strrchr (arg, '/');
		base = base ? base + 1 : arg;
		char stem[128], path[PATH_MAX];
		snprintf (stem, sizeof (stem), "%s", base);
		char *dot = strrchr (stem, '.');
		if (dot)
			*dot = 0;
		if (!*stem || !OwnedNameOk (stem))
			snprintf (stem, sizeof (stem), "data");
		snprintf (path, sizeof (path), "%s/%s%s.xur", dest, basedir ? basedir : "", stem);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (xur, 1, xur_size, F.f) != xur_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", xur_size, path);
		ResetFile (&F, opt_preserve);
	}
	// models: one GLB per "tcmd" object
	const uint n_models = is_trb && !testmode && !err ? CountBlueTongueModels (raw, raw_size) : 0;
	// batches sharing arrays by name (props placed in a cell) find them in the level's
	// LevelAssets.trb
	u8 *lib = 0;
	size_t lib_size = 0;
	if (n_models && !strstr (arg, "LevelAssets.trb"))
	{
		char lpath[PATH_MAX];
		snprintf (lpath, sizeof (lpath), "%s", arg);
		char *slash = strrchr (lpath, '/');
		snprintf (slash ? slash + 1 : lpath, lpath + sizeof (lpath) - (slash ? slash + 1 : lpath),
			"LevelAssets.trb");
		if (LoadFileAlloc (lpath, 0, 0, &lib, &lib_size, 0, 0, 0, false) || !lib
			|| !IsBlueTongueTrb (lib, lib_size))
		{
			FREE (lib);
			lib = 0;
			lib_size = 0;
		}
	}
	bttrb_tex_t *lib_tex = 0;
	bool *lib_tex_done = 0, lib_tex_tried = false;
	uint lib_tex_n = 0;
	char (*used)[64] = n_models ? CALLOC (n_models, 64) : 0;
	uint n_used = 0;
	for (uint i = 0; used && i < n_models && !err; i++)
	{
		char mname[64], clean[64];
		model_t *model
			= BuildBlueTongueModel (raw, raw_size, i, mname, sizeof (mname), lib, lib_size);
		if (!model)
			continue;
		snprintf (clean, sizeof (clean), "%s", mname);
		for (char *c = clean; *c; c++)
			if (!isalnum ((u8)*c) && *c != '_' && *c != '-' && *c != '.')
				*c = '_';
		if (!*clean)
			snprintf (clean, sizeof (clean), "model");
		char base[64];
		snprintf (base, sizeof (base), "%.56s", clean);
		for (uint k = 2;; k++)
		{
			bool clash = false;
			for (uint u = 0; u < n_used && !clash; u++)
				clash = !strcasecmp (used[u], clean);
			if (!clash)
				break;
			snprintf (clean, sizeof (clean), "%s_%u", base, k);
		}
		snprintf (used[n_used++], 64, "%s", clean);
		char mdir[PATH_MAX], path[PATH_MAX];
		snprintf (mdir, sizeof (mdir), "%s/%smodels", dest, basedir ? basedir : "");
		snprintf (path, sizeof (path), "%s/%s.glb", mdir, clean);
		if (!CreatePath (mdir, true))
		{
			// the diffuse maps the model's materials name live in the package the
			// materials were read from (the level's LevelAssets.trb for cell props)
			if (!lib_tex && !lib_tex_tried)
			{
				lib_tex_tried = true;
				lib_tex = ListBlueTongueTextures (lib ? lib : raw, lib ? lib_size : raw_size, &lib_tex_n);
				lib_tex_done = lib_tex_n ? CALLOC (lib_tex_n, sizeof (bool)) : 0;
			}
			for (size_t mi = 0; mi < model->num_materials && lib_tex_done; mi++)
			{
				if (!model->materials[mi].num_textures)
					continue;
				for (uint t = 0; t < lib_tex_n; t++)
				{
					if (lib_tex_done[t] || strcasecmp (lib_tex[t].name, model->materials[mi].textures[0]))
						continue;
					u8 *rgba = 0;
					char tpath[PATH_MAX];
					snprintf (tpath, sizeof (tpath), "%s/%s.png", mdir, lib_tex[t].name);
					if (!DecodeBlueTongueTexture (
							&rgba, lib ? lib : raw, lib ? lib_size : raw_size, lib_tex + t))
						SaveDecodedRGBAToPNG (
							rgba, lib_tex[t].width, lib_tex[t].height, &be_func, tpath, 0, true);
					lib_tex_done[t] = true;
					break;
				}
			}
			ExportModelToGLB (model, path);
		}
		FreeModel (model);
	}
	FREE (lib_tex);
	FREE (lib_tex_done);
	FREE (used);
	FREE (lib);
	FREE (list);
	FREE (raw);
	return err;
}
