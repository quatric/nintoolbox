// Sega ARCB archive (Super Monkey Ball: Banana Blitz) and bare AVLZ blocks;
// see lib-arcb.h.
static enumError extract_arcb_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size >= 12 && !memcmp (raw, "AVLZ", 4))
	{
		u8 *plain = 0;
		uint plain_size = 0;
		const enumError derr = DecodeAvlz (raw, raw_size, &plain, &plain_size);
		FREE (raw);
		if (derr)
			return ERR_NOTHING_TO_DO;
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.bin", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECOMPRESS AVLZ:%s -> %s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, dest);
		enumError err = ERR_OK;
		if (!testmode)
			err = SaveFile (dest, 0, 0, plain, plain_size, 0);
		FREE (plain);
		return err;
	}
	if (raw_size < 0x80 || memcmp (raw, "ARCB", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	const enumError serr = ScanArcb (&entries, &n_entries, raw, raw_size);
	FREE (raw);
	if (serr || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "ARCB", entries, n_entries);
}
