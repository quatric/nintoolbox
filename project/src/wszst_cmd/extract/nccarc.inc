static enumError extract_nccarc_file (ccp arg, ccp basedir, uint depth)
{
	// Matches "foo.nccarc" (uncompressed) and "foo.nccarc_c.bin" (this
	// fork's own LZSS layer already decompressed it to .bin) but not the
	// still-compressed "foo.nccarc_c" sitting beside it.
	if (!is_ext (arg, ".nccarc") && !(is_ext (arg, ".bin") && strstr (arg, ".nccarc_c.bin")))
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

	nccarc_t nc;
	err = ScanNCCARC (&nc, raw, (uint)raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NCCARC:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, nc.n_entries, dest);

	for (uint i = 0; !err && i < nc.n_entries; i++)
	{
		const nccarc_entry_t *e = nc.entries + i;
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%04u%s.bin", dest, basedir ? basedir : "", i,
			e->flag ? "_f" : "");
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->size && fwrite (e->data, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetNCCARC (&nc);
	FREE (raw);
	return err;
}
