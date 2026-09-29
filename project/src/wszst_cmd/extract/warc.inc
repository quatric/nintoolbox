// Extract a Game & Wario WARC archive ("WARC" magic, Wii U). Unlike PAC,
// entries do carry real filenames (plus a single flat folder-path prefix
// baked in by ScanWARC), so this writes them out directly rather than
// synthesising a name.
static enumError extract_warc_file (ccp arg, ccp basedir, uint depth)
{
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
	if (raw_size >= 8 && !memcmp (raw, "FZIP", 4))
	{
		u8 *dec = 0;
		uint dec_sz = 0;
		if (DecodeFZIP (&dec, &dec_sz, raw, (uint)raw_size) == ERR_OK && dec)
		{
			FREE (raw);
			raw = dec;
			raw_size = dec_sz;
		}
	}
	if (raw_size < 4 || memcmp (raw, "WARC", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	warc_t warc;
	err = ScanWARC (&warc, raw, (uint)raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT WARC:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, warc.n_entries, dest);

	for (uint i = 0; !err && i < warc.n_entries; i++)
	{
		const warc_entry_t *e = warc.entries + i;
		if (!e->name || testmode)
			continue;

		// ScanWARC() builds e->name straight out of the archive's own
		// folder/file string table (see lib-warc.c) with no ".." filtering;
		// an entry naming a folder like "../../tmp" would otherwise escape
		// 'dest'. Substitute a synthetic name rather than losing the whole
		// archive over one bad entry, matching the fallback used elsewhere.
		ccp name = e->name;
		char safe_name[32];
		if (!valid_sarc_path (name))
		{
			snprintf (safe_name, sizeof (safe_name), "%04u.bin", i);
			name = safe_name;
		}

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->size && fwrite (e->data, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetWARC (&warc);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
