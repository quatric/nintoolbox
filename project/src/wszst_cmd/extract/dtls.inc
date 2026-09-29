static enumError extract_dtls_file (ccp arg, ccp basedir, uint depth)
{
	ccp fname = FindFilename (arg, 0);
	// Retail Smash 4 ships the lookup as an extensionless `content/ls`
	// beside `content/dt00`; accept that exact basename too.
	if (!fname
		|| (!strstr (fname, "ls00") && !strstr (fname, "ls01") && !is_ext (arg, ".ls")
			&& strcmp (fname, "ls")))
		return ERR_NOTHING_TO_DO;

	u8 *ls_raw = 0;
	size_t ls_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &ls_raw, &ls_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	char dt_path[PATH_MAX];
	snprintf (dt_path, sizeof (dt_path), "%s", arg);
	char *ls_pos = strstr (dt_path, "ls00");
	if (!strcmp (fname, "ls"))
	{
		// Retail Smash 4 layout: extensionless lookup beside its data
		// file in the same directory (`content/ls` + `content/dt00` on
		// Wii U, `romfs/ls` + `romfs/dt` on 3DS).
		char *slash = strrchr (dt_path, '/');
		char *base = slash ? slash + 1 : dt_path;
		size_t room = sizeof (dt_path) - (size_t)(base - dt_path);
		snprintf (base, room, "dt00");
		struct stat dt_st;
		if (stat (dt_path, &dt_st) != 0 || !S_ISREG (dt_st.st_mode))
			snprintf (base, room, "dt");
	}
	else if (ls_pos)
		memcpy (ls_pos, "dt00", 4);
	else
	{
		ls_pos = strstr (dt_path, "ls01");
		if (ls_pos)
			memcpy (ls_pos, "dt01", 4);
		else
		{
			char *dot = strrchr (dt_path, '.');
			if (dot)
				snprintf (dot, sizeof (dt_path) - (dot - dt_path), ".dt");
		}
	}

	u8 *dt_raw = 0;
	size_t dt_size = 0;
	// The dt companion is optional (retail dt00 is 4 GB and usually
	// absent); stat first so a missing file doesn't print an error.
	struct stat dt_st;
	if (stat (dt_path, &dt_st) == 0 && S_ISREG (dt_st.st_mode))
		LoadFileAlloc (dt_path, 0, 0, &dt_raw, &dt_size, 0, 0, 0, false);

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanDTLS (&entries, &n_entries, ls_raw, (uint)ls_size, dt_raw, (uint)dt_size);
	if (err)
	{
		FREE (ls_raw);
		if (dt_raw)
			FREE (dt_raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT DTLS:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);

	if (!testmode)
	{
		for (uint i = 0; i < n_entries; i++)
		{
			char path[PATH_MAX];
			snprintf (
				path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", entries[i].name);
			File_t F;
			if (!CreateFileOpt (&F, true, path, false, arg) && F.f)
			{
				if (entries[i].size && entries[i].data)
					fwrite (entries[i].data, 1, entries[i].size, F.f);
				ResetFile (&F, opt_preserve);
			}
		}
	}

	ResetOwnedEntries (entries, n_entries);
	FREE (ls_raw);
	if (dt_raw)
		FREE (dt_raw);
	return ERR_OK;
}
