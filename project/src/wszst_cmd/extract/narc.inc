static enumError extract_narc_mem (ccp arg, ccp basedir, uint depth, const u8 *raw, size_t raw_size)
{
	if (!raw || raw_size < 16 || raw_size > UINT_MAX)
		return ERR_NOTHING_TO_DO;
	if (memcmp (raw, "NARC", 4) && memcmp (raw, "CRAN", 4))
		return ERR_NOTHING_TO_DO;

	narc_t narc;
	enumError err = ScanNARC (&narc, raw, raw_size);
	if (err)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NARC:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, narc.n_entries, dest);

	for (uint i = 0; !err && i < narc.n_entries; i++)
	{
		const narc_entry_t *e = narc.entries + i;
		if (!e->size)
			continue;
		char auto_name[PATH_MAX];
		ccp name = e->name;
		if (!name || !*name)
		{
			ccp ext = ".bin";
			if (e->offset + e->size <= narc.fimg_size)
			{
				const u8 *sub_d = narc.fimg_data + e->offset;
				if (e->size >= 4)
				{
					if (!memcmp (sub_d, "BY", 2) || !memcmp (sub_d, "YB", 2))
						ext = ".byml";
					else if (!memcmp (sub_d, "CTPK", 4))
						ext = ".ctpk";
					else if (!memcmp (sub_d, "CGFX", 4))
						ext = ".cgfx";
					else if (!memcmp (sub_d, "CLYT", 4))
						ext = ".bclyt";
					else if (!memcmp (sub_d, "CLAN", 4))
						ext = ".bclan";
					else if (!memcmp (sub_d, "DVLB", 4))
						ext = ".dvlb";
					else if (!memcmp (sub_d, "BMD0", 4))
						ext = ".nsbmd";
					else if (!memcmp (sub_d, "BTX0", 4))
						ext = ".nsbtx";
					else if (e->size >= 0x28
						&& (!memcmp (sub_d + e->size - 0x28, "CLIM", 4)
							|| !memcmp (sub_d + e->size - 0x28, "FLIM", 4)))
						ext = ".bclim";
				}
			}
			snprintf (auto_name, sizeof (auto_name), "file_%04u%s", i, ext);
			name = auto_name;
		}

		if (!valid_sarc_path (name))
		{
			snprintf (auto_name, sizeof (auto_name), "file_%04u.bin", i);
			name = auto_name;
		}
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->offset + e->size <= narc.fimg_size
			&& fwrite (narc.fimg_data + e->offset, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetNARC (&narc);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

static enumError extract_narc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".narc") && !is_ext (arg, ".carc") && !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}
	err = extract_narc_mem (arg, basedir, depth, raw, raw_size);
	FREE (raw);
	return err;
}
