// Vblank Entertainment Wii packages (Retro City Rampage DX / Shakedown: Hawaii
// gamedata_wii.bfp and audio_*_Wii.bap). Extract-only.
static enumError extract_vblank_file (ccp arg, ccp basedir, uint depth)
{
	const bool bfp = is_ext (arg, ".bfp");
	if (!bfp && !is_ext (arg, ".bap"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = bfp ? ScanVblankBfp (&entries, &n_entries, raw, raw_size)
			  : ScanVblankBap (&entries, &n_entries, raw, raw_size);
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (
		arg, basedir, depth, bfp ? "VBLANK-BFP" : "VBLANK-BAP", entries, n_entries);
}
