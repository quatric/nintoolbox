// Square Enix SEDB containers (Dragon Quest X, Wii). RES (.rps) archives are
// split into their sub-files; "txb" textures are decoded to a sibling PNG.
// See lib-sedb.h.
static enumError extract_sedb_res_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".rps"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size > UINT_MAX || !IsSEDBRes (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	const enumError serr = ScanSEDBRes (&entries, &n_entries, raw, (uint)raw_size);
	FREE (raw);
	if (serr || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "SEDB-RES", entries, n_entries);
}

static enumError extract_sedb_txb_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".txb"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size > UINT_MAX || !IsSEDBTexture (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *rgba = 0;
	uint w = 0, h = 0;
	const enumError err = DecodeSEDBTexture (&rgba, &w, &h, raw, (uint)raw_size);
	FREE (raw);
	if (err || !rgba)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_image_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SEDB-TXB:%s (%ux%u) -> PNG:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, w, h, dest);

	if (testmode)
	{
		FREE (rgba);
		return ERR_OK;
	}
	return SaveDecodedRGBAToPNG (rgba, w, h, &le_func, dest, 0, opt_overwrite);
}
