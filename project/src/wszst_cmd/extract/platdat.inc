// PlatinumGames "DAT" archive (".dat", MadWorld); see lib-platdat.h.
static enumError extract_platdat_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dat"))
		return ERR_NOTHING_TO_DO;

	FILE *src = fopen (arg, "rb");
	if (!src)
		return ERR_NOTHING_TO_DO;
	struct stat st;
	stream_entry_t *entries = 0;
	uint n_entries = 0;
	if (fstat (fileno (src), &st) || st.st_size <= 0
		|| ScanPlatDat (src, (u64)st.st_size, &entries, &n_entries) || !n_entries)
	{
		fclose (src);
		return ERR_NOTHING_TO_DO;
	}
	const enumError err
		= extract_streamed_entries (arg, basedir, depth, "PLATDAT", src, entries, n_entries);
	FreeStreamEntries (entries, n_entries);
	fclose (src);
	return err;
}
