// Same ordering problem as BFRES/BCRES: a real BMD0's textures live in a
// TEX0 dictionary block and are only decoded to PNG by ExportNitroTEX0Textures
// (the legacy pre-BMD0 flat layout ExportEarlyDSBMDTextures understands is a
// harmless no-op on these). export_model_if_possible() -- called later, from
// export_models_tree() -- writes those PNGs as a side effect of the very same
// pass that resolves texture references against SetDAETextureSearchRoot()'s
// static index, so a texture decoded there is always one pass too late to be
// found. Give NSBMD its own earlier pass, like FTEX/CGFX above.
static enumError extract_nsbmd_textures (ccp arg, ccp basedir, uint depth)
{
	const bool is_bmd_file = is_ext (arg, ".bmd") || is_ext (arg, ".nsbmd");
	if (!is_bmd_file)
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s", arg);
	char *slash = strrchr (dest, '/');
	if (slash)
		*slash = 0;
	else
		snprintf (dest, sizeof (dest), ".");

	if (!testmode)
	{
		ExportNitroTEX0Textures (raw, (uint)raw_size, dest);
		ExportEarlyDSBMDTextures (raw, raw_size, dest);
	}
	FREE (raw);
	(void)basedir;
	(void)depth;
	return ERR_NOTHING_TO_DO;
}

static enumError extract_nsbmd_textures_tree (ccp root, uint depth)
{
	if (depth > 32)
		return ERR_FILE_TOO_BIG;
	DIR *dir = opendir (root);
	if (!dir)
		return ERR_NOT_EXISTS;
	struct dirent *de;
	while ((de = readdir (dir)))
	{
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char path[PATH_MAX];
		const int len = snprintf (path, sizeof (path), "%s/%s", root, de->d_name);
		if (len < 0 || (uint)len >= sizeof (path))
			continue;
		struct stat st;
		if (lstat (path, &st))
			continue;
		if (S_ISDIR (st.st_mode))
			extract_nsbmd_textures_tree (path, depth + 1);
		else if (S_ISREG (st.st_mode))
			extract_nsbmd_textures (path, 0, depth);
	}
	closedir (dir);
	return ERR_OK;
}
