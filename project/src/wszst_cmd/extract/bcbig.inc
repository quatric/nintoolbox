// Blue Castle Games ".big" archives (The Bigs / The Bigs 2, Wii). The
// extension is shared with the EA "BIGF" archives, so the claim needs the
// 04 03 02 01 magic plus a geometry check on the header; see lib-bcbig.h.
static enumError extract_bcbig_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".big"))
		return ERR_NOTHING_TO_DO;

	u8 head[16];
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	const size_t got = fread (head, 1, sizeof (head), f);
	fclose (f);
	if (got < 4 || memcmp (head, "\x04\x03\x02\x01", 4))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanBcBig (&entries, &n_entries, raw, raw_size);
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "BCBIG", entries, n_entries);
}
