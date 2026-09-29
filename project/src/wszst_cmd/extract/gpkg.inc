// Gorilla Games' ".pkg" archive (Bonsai Barber and presumably this studio's
// other WiiWare titles). No container magic -- detection is entirely
// structural inside ScanGPKG() (zlib-decompress + header/table sanity), so
// this only pre-filters by extension to avoid even trying on files that
// couldn't plausibly be one.
static enumError extract_gpkg_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pkg"))
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

	gpkg_t pkg;
	err = ScanGPKG (&pkg, raw, raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PKG:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, pkg.n_entries, dest);

	for (uint i = 0; !err && i < pkg.n_entries; i++)
	{
		const gpkg_entry_t *e = pkg.entries + i;
		if (testmode || !e->data)
			continue;

		// e->name is a raw 0x20-byte field copied straight out of the
		// archive by ScanGPKG() with no sanitizing -- an entry named e.g.
		// "../../../tmp/evil" would otherwise escape 'dest'. Substitute a
		// synthetic name instead of aborting the whole archive over it, same
		// fallback the other named-entry extractors below use.
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

	ResetGPKG (&pkg);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
