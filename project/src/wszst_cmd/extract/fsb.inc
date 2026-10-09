// FMOD FSB3 / FSB4 / FSB5 sound banks (DSP-ADPCM and PCM16 samples decoded to WAV). Extract-only.
static enumError extract_fsb_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".fsb") && !is_ext (arg, ".strm"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanFSB (&entries, &n_entries, raw, raw_size);
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "FSB", entries, n_entries);
}
