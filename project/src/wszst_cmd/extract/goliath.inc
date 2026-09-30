// Goliath engine "GS" packages (Skylanders: SuperChargers Racing files/Data/
// *.pkz). Unrelated to the PlatinumGames .pkz handled by lib-pkz.c, so the
// scanner insists on the 0x80000001 chunk magic and a complete structural
// walk before it claims the file; see lib-goliath.h for the layout.
static enumError extract_goliath_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pkz"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (IsGoliathBlockPKZ (raw, raw_size))
	{
		u8 *plain = 0;
		size_t plain_size = 0;
		err = DecodeGoliathBlockPKZ (raw, raw_size, &plain, &plain_size);
		FREE (raw);
		if (err)
			return ERR_NOTHING_TO_DO;
		raw = plain;
		raw_size = plain_size;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanGoliathPKZ (&entries, &n_entries, raw, raw_size);
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "GOLIATH", entries, n_entries);
}
