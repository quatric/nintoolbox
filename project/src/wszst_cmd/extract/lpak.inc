// 2XL Games LPAK resource pack (SCORE International Baja 1000, Wii); see lib-lpak.h.
static enumError extract_lpak_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pak"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	const enumError serr = ScanLpak (&entries, &n_entries, raw, raw_size);
	FREE (raw);
	if (serr || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "LPAK", entries, n_entries);
}
