static enumError extract_thp_file (ccp arg, ccp basedir, uint depth)
{
	// THP is extracted as JPEG frames and raw audio chunks, but this command
	// has no corresponding THP builder.  Do not replace a packable game asset
	// with one-way previews during normal staging; XEXPORT is inspection mode.
	if (export_count <= 0)
		return ERR_NOTHING_TO_DO;

	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 head[48];
	size_t got = fread (head, 1, sizeof (head), f);
	fclose (f);
	if (got < 48 || memcmp (head, "THP\0", 4))
		return ERR_NOTHING_TO_DO;

	u8 *thp_data = 0;
	size_t thp_size_st = 0;
	enumError lerr = LoadFileAlloc (arg, 0, 0, &thp_data, &thp_size_st, 0, 0, 0, false);
	if (lerr || !thp_data || thp_size_st < 48)
	{
		FREE (thp_data);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	enumError err = ExtractTHP (&entries, &n_entries, thp_data, (uint)thp_size_st);
	FREE (thp_data);
	if (err || !n_entries)
	{
		if (entries)
		{
			for (uint i = 0; i < n_entries; i++)
			{
				FREE ((void *)entries[i].name);
				FREE ((void *)entries[i].data);
			}
			FREE (entries);
		}
		return err ? err : ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT THP:%s (%u frames/audio) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);

	for (uint i = 0; !err && i < n_entries; i++)
	{
		if (!valid_sarc_path (entries[i].name))
		{
			err = ERROR0 (ERR_INVALID_DATA, "Unsafe THP entry path: %s\n", entries[i].name);
			break;
		}

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", entries[i].name);
		if (testmode)
		{
			if (verbose >= 1)
				fprintf (stdlog, "  %s  %s\n", "WOULD WRITE", path);
			continue;
		}

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

	return err;
}
