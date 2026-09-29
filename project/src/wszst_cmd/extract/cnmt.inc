// Decode a Nintendo Switch Content Metadata (.cnmt) to text.
static enumError extract_cnmt_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".cnmt"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (!IsCNMT (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".txt");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CNMT:%s -> TEXT:%s\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	enumError err = ERR_OK;
	if (!testmode)
		err = SaveTextCNMT (dest, raw, raw_size);

	FREE (raw);
	return err;
}
