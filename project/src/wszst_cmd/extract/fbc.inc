// h.a.n.d. FBC bundles (Miffy no Omochabako, Wii); nested bundles are unpacked
// by the recursive extraction of the output tree.
static enumError extract_fbc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".fbc"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	const enumError serr = ScanFBC (&entries, &n_entries, raw, raw_size);
	FREE (raw);
	if (serr || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "FBC", entries, n_entries);
}
