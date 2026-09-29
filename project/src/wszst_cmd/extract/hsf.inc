static enumError extract_hsf_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".hsf"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 8 || memcmp (raw, "HSFV037", 7))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}

	if (g_in_model_deferred_pass)
	{
		FREE (raw);
		return ERR_OK;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");

	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT HSF:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}

	err = DecodeHSF (raw, (uint)raw_size, dest);
	FREE (raw);
	if (err)
		return err;

	// An unedited .hsf must not look newer than its .glb on the next CREATE
	// -- see StampFileMtime()'s doc comment -- or every model in a tree gets
	// needlessly re-encoded and repacked.
	StampFileMtime (dest, arg);

	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT HSF:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
