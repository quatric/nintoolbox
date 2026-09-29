// Donkey Konga (disc 1 only) "DKZF" trivial zlib texture wrapper
// (.tpl.dkz). Strips the wrapper to reveal the plain wrapped file --
// always a .tpl in every one of 64 real samples on the disc -- by dropping
// just the trailing ".dkz", then hands the revealed file back into the
// ordinary extraction chain so it decodes to .png the same way any other
// plain .tpl already does.
static enumError extract_dkzf_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dkz"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (!IsDKZF (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *dec = 0;
	uint dec_size = 0;
	const enumError derr = DecodeDKZF (raw, raw_size, &dec, &dec_size);
	FREE (raw);
	if (derr || !dec_size)
	{
		FREE (dec);
		return ERR_NOTHING_TO_DO;
	}

	// beside_source_dest_ext() strips the source's own rightmost extension
	// (".dkz") and appends 'new_ext' -- an empty string just leaves the real
	// extension underneath intact, e.g. "Banana.tpl.dkz" -> "Banana.tpl".
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, "");

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT DKZF:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);
	if (testmode)
	{
		FREE (dec);
		return ERR_OK;
	}

	enumError err = SaveFile (dest, 0, 0, dec, dec_size, 0);
	FREE (dec);
	if (!err)
	{
		const enumError sub_err = extract_one_file (dest, basedir, depth + 1);
		if (sub_err != ERR_NOTHING_TO_DO && err < sub_err)
			err = sub_err;
	}
	return err;
}
