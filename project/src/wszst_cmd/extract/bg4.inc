// BG4 ("BG4\0", Mario & Luigi: Paper Jam, 3DS), from mario_luigi_paper.bms.
static enumError extract_bg4_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size > UINT_MAX || raw_size < 16 || memcmp (raw, "BG4\0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	const enumError serr = ScanBG4 (&entries, &n_entries, raw, (uint)raw_size);
	FREE (raw);
	if (serr || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "BG4", entries, n_entries);
}
