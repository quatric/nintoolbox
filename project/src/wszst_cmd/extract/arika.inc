// Arika archive pair: INFO.DAT (obfuscated directory) beside GAME.DAT (the
// payload). Neither file has a magic signature to key off, so detection is
// by filename only, same convention arika.bms itself uses -- this also
// naturally skips GAME.DAT when the tree walker visits it directly (its
// basename never matches "INFO.DAT").
static enumError extract_arika_file (ccp arg, ccp basedir, uint depth)
{
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	if (strcasecmp (base, "INFO.DAT"))
		return ERR_NOTHING_TO_DO;

	u8 *info_data = 0;
	size_t info_size_st = 0;
	enumError lerr = LoadFileAlloc (arg, 0, 0, &info_data, &info_size_st, 0, 0, 0, false);
	if (lerr || !info_data || info_size_st < 0x30 || info_size_st > UINT_MAX)
	{
		FREE (info_data);
		return ERR_NOTHING_TO_DO;
	}

	// Sibling GAME.DAT, preserving this file's own case for the name.
	char game_path[PATH_MAX];
	snprintf (game_path, sizeof (game_path), "%.*s%s", (int)(base - arg), arg,
		base[0] >= 'a' && base[0] <= 'z' ? "game.dat" : "GAME.DAT");

	u8 *game_data = 0;
	size_t game_size_st = 0;
	lerr = LoadFileAlloc (game_path, 0, 0, &game_data, &game_size_st, 0, 0, 0, false);
	if (lerr || !game_data || game_size_st > UINT_MAX)
	{
		FREE (info_data);
		FREE (game_data);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	enumError err = ExtractArika (
		&entries, &n_entries, info_data, (uint)info_size_st, game_data, (uint)game_size_st);
	FREE (info_data);
	FREE (game_data);

	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT Arika:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);

	for (uint i = 0; !err && i < n_entries; i++)
	{
		// Same fallback as the RST loop above: don't abort the whole
		// archive over one unsafe/empty entry name, give it a synthetic
		// name instead.
		ccp name = entries[i].name;
		char safe_name[32];
		if (!valid_sarc_path (name))
		{
			snprintf (safe_name, sizeof (safe_name), "%04u.bin", i);
			name = safe_name;
		}
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && entries[i].size
			&& fwrite (entries[i].data, 1, entries[i].size, F.f) != entries[i].size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", entries[i].size, path);
		ResetFile (&F, opt_preserve);
	}

	for (uint i = 0; i < n_entries; i++)
	{
		FREE ((void *)entries[i].name);
		FREE ((void *)entries[i].data);
	}
	FREE (entries);

	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
