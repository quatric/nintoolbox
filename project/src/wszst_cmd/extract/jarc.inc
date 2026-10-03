// Extract Ganbarion JARC / jCMP archives (Wii Fit U, One Piece, etc.)
static enumError extract_jarc_file (ccp arg, ccp basedir, uint depth)
{
	bool is_jarc_ext = is_ext (arg, ".jarc") || is_ext (arg, ".jcmp");

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 16)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}

	bool is_jcmp_sig = !memcmp (raw, "jCMP", 4) || !memcmp (raw, "JCMP", 4);
	bool is_jarc_sig = !memcmp (raw, "jARC", 4) || !memcmp (raw, "JARC", 4);
	if (!is_jarc_ext && !is_jcmp_sig && !is_jarc_sig)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	jarc_t jarc;
	err = ScanJARC (&jarc, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT JARC:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, jarc.n_entries, dest);

	for (uint i = 0; !err && i < jarc.n_entries; i++)
	{
		const jarc_entry_t *e = jarc.entries + i;
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", e->name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->size && e->data && fwrite (e->data, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		const enumError close_err = ResetFile (&F, opt_preserve);
		if (close_err > err)
			err = close_err;
	}

	ResetJARC (&jarc);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
