// "DC2" engine (Jakers! Kart Racing, Wii): .dcx archives and .dct textures.
static enumError extract_dcx_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dcx"))
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

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanDCX (&entries, &n_entries, raw, (uint)raw_size);
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "DCX", entries, n_entries);
}
