static enumError extract_effn_file (ccp arg, ccp basedir, uint depth)
{
	u8 magic[4];
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	size_t n = fread (magic, 1, 4, f);
	fclose (f);
	if (n < 4 || memcmp (magic, "EFFN", 4) != 0)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT EFFN:%s -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (testmode)
		return ERR_OK;

	enumError err = ExtractEFFNArchive (arg, dest);
	if (!err && opt_recurse > depth)
		extract_tree_complete (dest, depth + 1);
	return err;
}
