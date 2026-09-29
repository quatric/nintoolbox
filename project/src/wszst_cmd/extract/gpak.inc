// 2D Boy's "master.pak" (World of Goo). No filenames are stored in the
// format at all (only a 32-bit hash), so members are extracted under
// ordinal names -- same convention already used for FSYS archives whose
// entries are all literally named "(null)" (see extract_fsys_file() below).
static enumError extract_gpak_file (ccp arg, ccp basedir, uint depth)
{
	// .gpak is the disambiguating extension CREATE writes, since .pak is
	// shared with Retro Studios' RPAK.
	if (!is_ext (arg, ".pak") && !is_ext (arg, ".gpak"))
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

	gpak_t pak;
	err = ScanGPAK (&pak, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PAK:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, pak.n_entries, dest);

	for (uint i = 0; !err && i < pak.n_entries; i++)
	{
		const gpak_entry_t *e = pak.entries + i;
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%sfile_%04u.bin", dest, basedir ? basedir : "", i);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->size && fwrite (e->data, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetGPAK (&pak);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
