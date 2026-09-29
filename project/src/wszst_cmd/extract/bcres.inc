static enumError extract_bcres_textures (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bcres") && !is_ext (arg, ".cgfx") && !is_ext (arg, ".bcmdl")
		&& !is_ext (arg, ".bcfnt"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 0x20 || memcmp (raw, "CGFX", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	ccp dest_base = opt_dest && *opt_dest ? opt_dest : basedir;
	if (dest_base && *dest_base)
		snprintf (dest, sizeof (dest), "%s", dest_base);
	else
	{
		snprintf (dest, sizeof (dest), "%s", arg);
		char *slash = strrchr (dest, '/');
		if (slash)
			*slash = 0;
		else
			snprintf (dest, sizeof (dest), ".");
	}

	if (!testmode)
		ExportBCRESTexturesFromData (raw, raw_size, dest);
	FREE (raw);
	(void)depth;
	return ERR_NOTHING_TO_DO;
}

static enumError extract_bcres_textures_tree (ccp root, uint depth)
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
			extract_bcres_textures_tree (path, depth + 1);
		else if (S_ISREG (st.st_mode))
			extract_bcres_textures (path, 0, depth);
	}
	closedir (dir);
	return ERR_OK;
}
