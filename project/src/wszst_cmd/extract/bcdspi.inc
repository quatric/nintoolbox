// Blue Castle Games ".dspi" audio (The Bigs / The Bigs 2, Wii): one DSP
// header per channel followed by the channel bodies. Each channel is written
// as a plain .dsp that the normal DSP decoder turns into a WAV; see
// lib-bcbig.h. A size rule over the headers gates the claim.
static enumError extract_bcdspi_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dspi"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanBcDspi (&entries, &n_entries, raw, raw_size);
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "BCDSPI", entries, n_entries);
}
