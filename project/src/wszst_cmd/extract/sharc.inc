static enumError extract_sharc_manifest (ccp arg)
{
	const bool is_fb = is_ext (arg, ".sharcfb");
	if (!is_fb && !is_ext (arg, ".sharc"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (is_fb ? !IsSHARCFB (raw, raw_size) : !IsSHARC (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Source archives are extracted into an editable directory (sources,
	// macros, tables) when that provably rebuilds to the same bytes.
	if (!is_fb && !testmode)
	{
		char dir[PATH_MAX];
		beside_source_dest (dir, sizeof (dir), arg);
		err = ExtractSHARCDir (raw, raw_size, dir, arg);
		if (err != ERR_NOTHING_TO_DO)
		{
			if (verbose >= 0)
				fprintf (stdlog, "%sEXTRACT SHARC:%s -> %s/\n", verbose > 0 ? "\n" : "", arg, dir);
			FREE (raw);
			return err;
		}
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE %s:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", is_fb ? "SHARCFB" : "SHARC", arg, dest);

	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f)
		err = is_fb ? DecodeSHARCFB_Text (F.f, raw, raw_size)
					: DecodeSHARC_Text (F.f, raw, raw_size);
	ResetFile (&F, opt_preserve);
	FREE (raw);
	return err;
}
