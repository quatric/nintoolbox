//-----------------------------------------------------------------------------
// HAL Laboratory "A2" bank archives -- Kirby Air Ride's 2D/UI ".dat" files.
// Same extension as the sysdolphin archives above and equally magic-less, but
// a completely different (and much simpler) named-blob container, so it gets
// its own probe; IsHSD() and IsHALBank() are mutually exclusive on the real
// disc. Destination handling and return value follow extract_hsd_textures()
// above -- see its comment.

static enumError extract_halbank_textures (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dat") && !is_ext (arg, ".usd"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 0x18 || raw_size > 0x40000000 || !IsHALBank (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	hal_texture_dest (dest, sizeof (dest), arg);
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;

	if (!testmode)
	{
		const int n = ExportHALBankTexturesFromData (raw, (uint)raw_size, dest, base);
		if (n > 0 && verbose >= 0)
			fprintf (stdlog, "  - %u HAL bank texture%s extracted from %s -> %s/\n", n,
				n == 1 ? "" : "s", arg, dest);
	}
	else if (verbose >= 0)
		fprintf (stdlog, "  - WOULD extract HAL bank textures from %s -> %s/\n", arg, dest);
	FREE (raw);
	(void)basedir;
	(void)depth;
	return ERR_OK;
}

static enumError extract_halbank_textures_tree (ccp root, uint depth)
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
			extract_halbank_textures_tree (path, depth + 1);
		else if (S_ISREG (st.st_mode))
			extract_halbank_textures (path, 0, depth);
	}
	closedir (dir);
	return ERR_OK;
}
