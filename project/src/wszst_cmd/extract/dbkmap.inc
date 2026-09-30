// Artefacts Studio ".map" level databases (Diabolik: The Original Sin, Boot
// Camp Academy, ... on Wii). The "FAAFFAAF" tagged-section stream is shared
// with the small .cfg/.gam/.loc files, so the claim needs the stream header
// and closing-tag check of IsDiabolikRes(); see lib-dbkmap.h.
static enumError extract_dbkmap_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".map"))
		return ERR_NOTHING_TO_DO;

	u8 head[8];
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	const size_t got = fread (head, 1, sizeof (head), f);
	fclose (f);
	if (got < 8 || memcmp (head, "\xfa\xaf\xfa\xaf", 4))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanDbkMap (&entries, &n_entries, raw, raw_size);
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "DBKMAP", entries, n_entries);
}
