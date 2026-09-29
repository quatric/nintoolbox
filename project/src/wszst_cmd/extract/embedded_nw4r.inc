// Last-resort fallback: scan an otherwise-unrecognized, sizable file for
// embedded self-describing NW4R RLYT/RLAN resources (see ScanEmbeddedNW4R()
// in lib-nintendo.h for why -- We Ski's DATA/files/SKI.DAT, a fully custom
// Namco engine format with no discoverable container of its own). Gated to
// files over 1MB (anything smaller is either already handled by a real
// extractor above, or too small to be worth a full-file byte scan) and
// requires at least one hit, so the common case (a file with no embedded
// NW4R resources at all) costs one full-file scan and nothing else.
static enumError extract_embedded_nw4r_file (ccp arg, ccp basedir, uint depth)
{
	struct stat st;
	if (stat (arg, &st) != 0 || st.st_size < 0x100000)
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 2, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nw4r_embedded_t found;
	ScanEmbeddedNW4R (&found, raw, raw_size);
	if (!found.n_entries)
	{
		ResetEmbeddedNW4R (&found);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog,
			"%s%sEXTRACT embedded NW4R:%s (%u resources found in unrecognized data) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, found.n_entries, dest);

	for (uint i = 0; !err && i < found.n_entries; i++)
	{
		const nw4r_embedded_entry_t *e = found.entries + i;
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s_%04u.%s", dest, basedir ? basedir : "",
			e->is_brlan ? "rlan" : "rlyt", i, e->is_brlan ? "brlan" : "brlyt");
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (e->data, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetEmbeddedNW4R (&found);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
