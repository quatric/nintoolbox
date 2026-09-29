// Extract a Good-Feel archive (GFAC).  The whole payload is one GFCP-
// compressed blob; ScanGFA decompresses it and returns the member table.
// Entries with size 0 are directory markers: the reference tooling treats
// each as the parent directory for the entries that follow it.
static enumError extract_gfa_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".gfa") && !is_ext (arg, ".bin"))
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
	if (raw_size < 4 || memcmp (raw, "GFAC", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	gfa_t gfa;
	err = ScanGFA (&gfa, raw, raw_size);
	FREE (raw);
	if (err)
		return err;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT GFA:%s (%u entries, %s) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, gfa.n_entries, gfa.compression == 1 ? "BPE" : "LZ10",
			dest);

	ccp subdir = "";
	for (uint i = 0; !err && i < gfa.n_entries; i++)
	{
		const gfa_entry_t *e = gfa.entries + i;
		if (!e->size)
		{
			// directory marker
			subdir = e->name;
			continue;
		}
		char rel[PATH_MAX];
		if (subdir && *subdir)
			snprintf (rel, sizeof (rel), "%s/%s", subdir, e->name);
		else
			snprintf (rel, sizeof (rel), "%s", e->name);
		if (!valid_sarc_path (rel))
			// Same fallback as the SARC/RST/Arika loops: don't abort every
			// remaining entry over one unsafe/non-UTF8 name.
			snprintf (rel, sizeof (rel), "file_%04u.bin", i);
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", rel);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (gfa.blob + e->offset, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetGFA (&gfa);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;

		// GFA extraction (unlike the generic SZS pipeline) never writes a
		// wszst-setup.txt marker into 'dest', so CREATE's staleness check
		// falls back to normalize_legacy_tree_mtimes(), which folds any
		// container/decoder clock skew into a single baseline by stamping
		// every member's mtime down to the archive's own mtime. Do that fold
		// once, right now, before the user can touch anything: a later CREATE
		// run then only ever finds this baseline already in place (see the
		// SZS_MTIME_BASELINE_FILE guard in create_archive_dispatch.inc) and compares a real
		// edit's mtime against it untouched, instead of re-running the fold
		// and silently erasing the very edit it's supposed to detect.
		struct stat st_arg;
		if (!stat (arg, &st_arg))
		{
			normalize_legacy_tree_mtimes (dest, st_arg.st_mtime);
			char baseline_marker[PATH_MAX];
			snprintf (
				baseline_marker, sizeof (baseline_marker), "%s/%s", dest, SZS_MTIME_BASELINE_FILE);
			FILE *mf = fopen (baseline_marker, "w");
			if (mf)
				fclose (mf);
		}
	}
	return err;
}
