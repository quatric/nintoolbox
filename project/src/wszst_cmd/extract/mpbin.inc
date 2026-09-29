static enumError extract_mpbin_file (ccp arg, ccp basedir, uint depth)
{
	if (is_ext (arg, ".body") || is_ext (arg, ".bmsh") || is_ext (arg, ".gshd"))
		return ERR_NOTHING_TO_DO; // Grip .res object sections
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err || raw_size < 16 || raw_size > UINT_MAX || !IsMPBIN (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanMPBIN (&entries, &n_entries, raw, (uint)raw_size);
	FREE (raw);
	if (err)
		return err;
	if (!n_entries)
		return ERR_INVALID_DATA;

	return write_owned_entries (arg, basedir, depth, "MPBIN", entries, n_entries);
}
