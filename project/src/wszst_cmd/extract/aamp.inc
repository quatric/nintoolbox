static enumError extract_aamp_manifest (ccp arg)
{
	if (!is_ext (arg, ".aamp") && !is_ext (arg, ".baamp") && !is_ext (arg, ".bparam")
		&& !is_ext (arg, ".bgenv") && !is_ext (arg, ".bglpbd"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsAAMP (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, "\1N.aamp.yml", ".aamp.yml", false);
	else
		snprintf (dest, sizeof (dest), "%s.yml", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE AAMP:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f)
	{
		ccp ext = strrchr (dest, '.');
		if (ext && !strcasecmp (ext, ".json"))
			err = DecodeAAMP_JSON (F.f, raw, raw_size);
		else
			err = DecodeAAMP_YAML (F.f, raw, raw_size);
	}
	const enumError close_err = ResetFile (&F, opt_preserve);
	if (close_err > err)
		err = close_err;
	FREE (raw);
	return err;
}
