// Decode a Nintendo Switch Application Control Property (.nacp / control.nacp) to text.
static enumError extract_nacp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".nacp"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size > UINT_MAX || !IsNACP (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nacp_t nacp;
	enumError err = ParseNACP (&nacp, raw, (uint)raw_size);
	FREE (raw);
	if (err)
		return err;

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".txt");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NACP:%s -> TEXT:%s (version %s)\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, nacp.display_version);

	if (!testmode)
		err = SaveTextNACP (&nacp, dest);

	return err;
}
