// Decode raw Nintendo streams found below an extracted archive.  Sources stay
// in place and their decoded payload is written beside them, which preserves
// the project tree for deterministic archive rebuilds.
static enumError auto_decompress_tree (ccp root, uint depth)
{
	if (depth > 32)
		return ERR_FILE_TOO_BIG;
	DIR *dir = opendir (root);
	if (!dir)
		return ERR_NOT_EXISTS;
	enumError max_err = ERR_OK;
	struct dirent *de;
	while ((de = readdir (dir)))
	{
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char path[PATH_MAX];
		const int len = snprintf (path, sizeof (path), "%s/%s", root, de->d_name);
		if (len < 0 || (uint)len >= sizeof (path))
		{
			max_err = ERR_FILE_TOO_BIG;
			continue;
		}
		struct stat st;
		if (lstat (path, &st))
		{
			if (max_err < ERR_NOT_EXISTS)
				max_err = ERR_NOT_EXISTS;
			continue;
		}
		enumError err = ERR_OK;
		if (S_ISDIR (st.st_mode))
			err = auto_decompress_tree (path, depth + 1);
		else if (S_ISREG (st.st_mode))
			err = decompress_nintendo_file (path);
		if (err != ERR_NOTHING_TO_DO && max_err < err)
			max_err = err;
	}
	closedir (dir);
	return max_err;
}

static void get_extract_dest (char *dest, uint dest_size, const szs_file_t *szs)
{
	ccp pattern = opt_dest;
	if (!pattern)
		switch (szs->fform_arch)
		{
			case FF_U8:
			case FF_WU8:
			case FF_LTA:
			case FF_LFL:
			case FF_RARC:
			case FF_PACK:
			case FF_RKC:
				pattern = "\1P/\1N.d/";
				break;
			default:
				pattern = "\1P/\1F.d/";
				break;
		}
	SubstDest (dest, dest_size, szs->fname, pattern, 0, 0, true);
}

//
///////////////////////////////////////////////////////////////////////////////
