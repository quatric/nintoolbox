static enumError extract_ue4_pak_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pak") && !is_ext (arg, ".bin") && !is_ext (arg, ".ue4"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsUE4Pak (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	ue4_pak_t pak;
	err = ScanUE4Pak (&pak, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT UE4-PAK (Mario & Luigi: Brothership):%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, pak.n_entries, dest);

	for (uint i = 0; !err && i < pak.n_entries; i++)
	{
		if (testmode)
			continue;

		u8 *entry_data = 0;
		size_t entry_size = 0;
		enumError eerr = ExtractUE4PakEntry (&pak, i, &entry_data, &entry_size);
		if (eerr)
			continue;

		const char *fname = pak.entries[i].filename;
		while (*fname == '/' || *fname == '\\' || !strncmp (fname, "../", 3)
			|| !strncmp (fname, "..\\", 3))
		{
			if (*fname == '/' || *fname == '\\')
				fname++;
			else
				fname += 3;
		}

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", fname);

		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && entry_size > 0 && fwrite (entry_data, 1, entry_size, F.f) != entry_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", entry_size, path);
		ResetFile (&F, opt_preserve);
		FREE (entry_data);
	}

	ResetUE4Pak (&pak);
	FREE (raw);

	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
