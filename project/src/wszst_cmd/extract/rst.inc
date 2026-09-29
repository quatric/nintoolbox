static enumError extract_rst_file (ccp arg, ccp basedir, uint depth)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 head[64];
	size_t got = fread (head, 1, sizeof (head), f);
	fclose (f);
	if (got < 64 || (memcmp (head, "0TSR", 4) && memcmp (head, "RST0", 4)))
		return ERR_NOTHING_TO_DO;

	char toc_path[PATH_MAX];
	snprintf (toc_path, sizeof (toc_path), "%s", arg);
	char *dot = strrchr (toc_path, '.');
	if (dot)
		snprintf (dot, sizeof (toc_path) - (dot - toc_path), ".toc");
	else
		snprintf (toc_path + strlen (toc_path), sizeof (toc_path) - strlen (toc_path), ".toc");

	u8 *toc_data = 0;
	uint toc_size = 0;
	FILE *ftoc = fopen (toc_path, "rb");
	if (ftoc)
	{
		fseek (ftoc, 0, SEEK_END);
		toc_size = (uint)ftell (ftoc);
		fseek (ftoc, 0, SEEK_SET);
		toc_data = MALLOC (toc_size);
		if (fread (toc_data, 1, toc_size, ftoc) != toc_size)
		{
			FREE (toc_data);
			toc_data = 0;
			toc_size = 0;
		}
		fclose (ftoc);
	}

	// WiiWare bundles the otherwise sibling .toc files inside tocres.res.
	if (!toc_data)
	{
		char tocres_path[PATH_MAX];
		ccp slash = strrchr (arg, '/');
		snprintf (tocres_path, sizeof (tocres_path), "%.*stocres.res",
			slash ? (int)(slash - arg + 1) : 0, arg);
		u8 *index_data = 0;
		size_t index_size = 0;
		// optional: a nested RST (e.g. extracted from info.res) has no
		// tocres.res beside it, which is not an error
		if (!access (tocres_path, R_OK)
			&& !LoadFileAlloc (tocres_path, 0, 0, &index_data, &index_size, 0, 0, 0, false))
		{
			nintendo_sarc_entry_t *toc_entries = 0;
			uint n_toc_entries = 0;
			if (!ExtractRST (&toc_entries, &n_toc_entries, index_data, (uint)index_size, 0, 0))
			{
				ccp base = slash ? slash + 1 : arg;
				char wanted[PATH_MAX];
				snprintf (wanted, sizeof (wanted), "%s", base);
				char *ext = strrchr (wanted, '.');
				if (ext)
					snprintf (ext, sizeof (wanted) - (ext - wanted), ".toc");
				for (uint i = 0; i < n_toc_entries; i++)
					if (!strcasecmp (toc_entries[i].name, wanted))
					{
						toc_size = toc_entries[i].size;
						toc_data = MALLOC (toc_size);
						memcpy (toc_data, toc_entries[i].data, toc_size);
						break;
					}
				for (uint i = 0; i < n_toc_entries; i++)
				{
					FREE ((void *)toc_entries[i].name);
					FREE ((void *)toc_entries[i].data);
				}
				FREE (toc_entries);
			}
			FREE (index_data);
		}
	}

	u8 *car_data = 0;
	size_t car_size_st = 0;
	enumError lerr = LoadFileAlloc (arg, 0, 0, &car_data, &car_size_st, 0, 0, 0, false);
	if (lerr || !car_data)
	{
		FREE (toc_data);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	enumError err
		= ExtractRST (&entries, &n_entries, car_data, (uint)car_size_st, toc_data, toc_size);
	FREE (car_data);
	FREE (toc_data);

	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT RST:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);

	for (uint i = 0; !err && i < n_entries; i++)
	{
		// An unsafe/empty name (seen on real Excite Truck .car archives --
		// some TOC entries resolve to an empty string, likely internal/
		// placeholder table slots rather than real path-traversal attempts)
		// used to abort the *entire* archive's extraction on the first
		// occurrence, silently dropping every entry after it. Give it a
		// synthetic index-based name instead and keep going, same as the
		// RPAK collision fallback -- no data lost, and genuinely malicious
		// paths (../, absolute, embedded backslash) still can't escape
		// 'dest' since they still get this same fallback rather than being
		// used verbatim.
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
		if (F.f && fwrite (entries[i].data, 1, entries[i].size, F.f) != entries[i].size)
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
