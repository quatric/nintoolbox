// Hyrule Warriors Legends (3DS) .idx/.bin pair, from
// hyrule_warriors_legends.bms. Neither half has a magic, so this is gated on
// the ".idx" extension plus an existing sibling ".bin" -- and ScanHWLegends
// then rejects the pair outright unless every table record addresses a real,
// in-bounds region of that .bin. Visiting the .bin directly does nothing
// (its extension never matches), so members are never extracted twice.
static enumError extract_hwlegends_file (ccp arg, ccp basedir, uint depth)
{
	const uint len = (uint)strlen (arg);
	if (len < 5 || strcasecmp (arg + len - 4, ".idx"))
		return ERR_NOTHING_TO_DO;

	char bin_path[PATH_MAX];
	snprintf (bin_path, sizeof (bin_path), "%.*s%s", (int)(len - 4), arg,
		arg[len - 3] >= 'a' && arg[len - 3] <= 'z' ? ".bin" : ".BIN");
	struct stat st_bin;
	if (stat (bin_path, &st_bin))
		snprintf (bin_path, sizeof (bin_path), "%.*s%s", (int)(len - 4), arg,
			arg[len - 3] >= 'a' && arg[len - 3] <= 'z' ? ".BIN" : ".bin");

	u8 *idx = 0, *bin = 0;
	size_t idx_size = 0, bin_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &idx, &idx_size, 0, 0, 0, false) || !idx
		|| LoadFileAlloc (bin_path, 0, 0, &bin, &bin_size, 0, 0, 0, false) || !bin
		|| idx_size > UINT_MAX || bin_size > UINT_MAX)
	{
		FREE (idx);
		FREE (bin);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	const enumError serr
		= ScanHWLegends (&entries, &n_entries, idx, (uint)idx_size, bin, (uint)bin_size);
	FREE (idx);
	FREE (bin);
	if (serr || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "HWL-idx", entries, n_entries);
}
