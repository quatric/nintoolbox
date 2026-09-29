// Smart Bomb BombShell engine data pack (Bee Movie Game .xwi / .xdx9):
// textures -> PNG and sounds (FSB3 banks on Wii, WAVs on PC) into
// DIR_TYPE/textures and DIR_TYPE/sounds in a directory named after the file.
static enumError extract_bombshell_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".xwi") && !is_ext (arg, ".xdx9"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	uint count = 0;
	bombshell_asset_t *list
		= IsBombshellPack (raw, raw_size) ? ListBombshell (raw, raw_size, &count) : 0;
	if (!list || !count)
	{
		FREE (list);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BOMBSHELL:%s (%u assets) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);

	static const ccp dir_names[] = { "car", "interface", "item", "link", "world", "colliders",
		"world_texture", "geometry", "share", "audio", "music" };
	enumError err = ERR_OK;
	for (uint i = 0; i < count && !err && !testmode; i++)
	{
		const bombshell_asset_t *a = list + i;
		char sub[64], path[PATH_MAX];
		snprintf (sub, sizeof (sub), "%u_%s", a->dir,
			a->dir_type < sizeof (dir_names) / sizeof (*dir_names) ? dir_names[a->dir_type]
																   : "data");
		if (a->kind == BSA_TEXTURE)
		{
			u8 *rgba = 0;
			snprintf (path, sizeof (path), "%s/%s%s/textures/%s.png", dest, basedir ? basedir : "",
				sub, a->name);
			if (!DecodeBombshellTexture (&rgba, raw, raw_size, a))
				err = SaveDecodedRGBAToPNG (rgba, a->width, a->height, &be_func, path, 0, true);
			if (!err && a->off_aux)
			{
				snprintf (path, sizeof (path), "%s/%s%s/textures/%s.aux.bin", dest,
					basedir ? basedir : "", sub, a->name);
				const u32 n = a->size * 2 < raw_size - a->off_aux ? a->size * 2
																  : (u32)(raw_size - a->off_aux);
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && fwrite (raw + a->off_aux, 1, n, F.f) != n)
					err = FILEERROR1 (
						&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", n, path);
				ResetFile (&F, opt_preserve);
			}
		}
		else
		{
			snprintf (path, sizeof (path), "%s/%s%s/sounds/%s.%s", dest, basedir ? basedir : "",
				sub, a->name, a->ext);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (raw + a->off, 1, a->size, F.f) != a->size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", a->size, path);
			ResetFile (&F, opt_preserve);
		}
	}
	// Wii models: one GLB per model record
	const uint n_models = !err && !testmode ? CountBombshellModels (raw, raw_size) : 0;
	char (*used)[64] = n_models ? CALLOC (n_models, 64) : 0;
	uint n_used = 0;
	for (uint i = 0; used && i < n_models && !err; i++)
	{
		char mname[64];
		uint mdir_k = 0, mdir_type = 0;
		model_t *model = BuildBombshellModel (
			raw, raw_size, i, mname, sizeof (mname), list, count, &mdir_k, &mdir_type);
		if (!model)
			continue;
		char clean[64], base[64];
		snprintf (clean, sizeof (clean), "%s", *mname ? mname : "model");
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
		snprintf (mdir, sizeof (mdir), "%s/%s%u_%s/models", dest, basedir ? basedir : "", mdir_k,
			mdir_type < sizeof (dir_names) / sizeof (*dir_names) ? dir_names[mdir_type] : "data");
		snprintf (path, sizeof (path), "%s/%s.glb", mdir, clean);
		if (!CreatePath (mdir, true))
			ExportModelToGLB (model, path);
		FreeModel (model);
	}
	FREE (used);
	FREE (list);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
