// Kuju kRAW music (Geometry Wars: Galaxies): PCM16 to WAV. Extract-only.
static enumError extract_kraw_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".kraw"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanKRAW (&entries, &n_entries, raw, raw_size);
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "KRAW", entries, n_entries);
}
