// Extract a Sound Archive file (.bcsar, .bfsar, .bcwar, .bfwar, .bcgrp, .bfgrp, .bin, .arc).
static enumError extract_sar_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bcsar") && !is_ext (arg, ".bfsar") && !is_ext (arg, ".bcwar")
		&& !is_ext (arg, ".bfwar") && !is_ext (arg, ".bcgrp") && !is_ext (arg, ".bfgrp")
		&& !is_ext (arg, ".csar") && !is_ext (arg, ".fsar") && !is_ext (arg, ".cwar")
		&& !is_ext (arg, ".fwar") && !is_ext (arg, ".cgrp") && !is_ext (arg, ".fgrp")
		&& !is_ext (arg, ".bin") && !is_ext (arg, ".arc"))
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
	if (raw_size < 0x20)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	sound_archive_t sar;
	err = ScanSoundArchive (&sar, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT %s:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", sar.magic, arg, sar.n_entries, dest);

	enumError last_write_err = ERR_OK;
	for (uint i = 0; i < sar.n_entries; i++)
	{
		const sar_file_entry_t *e = sar.entries + i;
		if (!e->data || !e->size)
			continue;
		if (testmode)
			continue;

		char filename[PATH_MAX];
		if (e->name && *e->name)
		{
			char clean_name[128];
			size_t k = 0;
			for (size_t j = 0; e->name[j] && k + 1 < sizeof (clean_name); j++)
			{
				unsigned char c = (unsigned char)e->name[j];
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
					|| c == '_' || c == '-' || c == '.')
					clean_name[k++] = (char)c;
				else
					clean_name[k++] = '_';
			}
			clean_name[k] = 0;
			if (k > 0)
				snprintf (filename, sizeof (filename), "%04u_%s%s", e->file_id, clean_name, e->ext);
			else
				snprintf (filename, sizeof (filename), "%04u%s", e->file_id, e->ext);
		}
		else
			snprintf (filename, sizeof (filename), "%04u%s", e->file_id, e->ext);

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", filename);
		File_t F;
		enumError ferr = CreateFileOpt (&F, true, path, false, arg);
		if (!ferr && F.f && e->size && fwrite (e->data, 1, e->size, F.f) != e->size)
			ferr
				= FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
		if (ferr && !last_write_err)
			last_write_err = ferr;
	}
	err = last_write_err;

	ResetSoundArchive (&sar);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
