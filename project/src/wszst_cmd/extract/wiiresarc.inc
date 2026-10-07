// "wii\0" resource archive (".ARC", Petanque Master); see lib-wiiresarc.h.
static enumError extract_wiiresarc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".arc"))
		return ERR_NOTHING_TO_DO;

	FILE *src = fopen (arg, "rb");
	if (!src)
		return ERR_NOTHING_TO_DO;
	struct stat st;
	stream_entry_t *entries = 0;
	uint n_entries = 0;
	if (fstat (fileno (src), &st) || st.st_size <= 0
		|| ScanWiiResArc (src, (u64)st.st_size, &entries, &n_entries) || !n_entries)
	{
		fclose (src);
		return ERR_NOTHING_TO_DO;
	}
	const enumError err
		= extract_streamed_entries (arg, basedir, depth, "WII-ARC", src, entries, n_entries);
	FreeStreamEntries (entries, n_entries);
	fclose (src);
	return err;
}
