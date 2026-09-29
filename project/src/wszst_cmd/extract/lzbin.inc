static enumError extract_lzbin_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err || raw_size < 16 || raw_size > UINT_MAX || !IsLZBIN (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanLZBIN (&entries, &n_entries, raw, (uint)raw_size);
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;

	return write_owned_entries (arg, basedir, depth, "LZBIN", entries, n_entries);
}
