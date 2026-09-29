// Xenoblade Chronicles 3D "cram" .arc (3DS), from xenoblade_arc.bms.
static enumError extract_cram_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size > UINT_MAX || raw_size < 16 || memcmp (raw, "cram", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	const enumError serr = ScanCramARC (&entries, &n_entries, raw, (uint)raw_size);
	FREE (raw);
	if (serr || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "cram", entries, n_entries);
}
