// Town Factory "PCKG" packages (Little King's Story, Wii); see lib-pckg.h.
// The extension varies (.pac, .pcha, .pac0-9, .bin, .dat), so the claim rests
// on the magic plus a full walk of the entry chain.
static enumError extract_pckg_file (ccp arg, ccp basedir, uint depth)
{
	u8 head[4];
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	const size_t got = fread (head, 1, sizeof (head), f);
	fclose (f);
	if (got < 4 || memcmp (head, "PCKG", 4))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanPckg (&entries, &n_entries, raw, raw_size);
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "PCKG", entries, n_entries);
}
