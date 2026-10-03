static enumError extract_numatb_manifest (ccp arg)
{
	if (!is_ext (arg, ".numatb"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsNUMATB (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE NUMATB:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f)
		err = DecodeNUMATB_Text (F.f, raw, raw_size);
	const enumError close_err = ResetFile (&F, opt_preserve);
	if (close_err > err)
		err = close_err;
	if (err)
	{
		FREE (raw);
		return err;
	}

	// A MatLab-dialect MaterialLibrary XML beside the text manifest, so the
	// material parameters are machine-editable and comparable with MatLab.
	char xdest[PATH_MAX];
	if (opt_dest)
		SubstDest (xdest, sizeof (xdest), arg, opt_dest, 0, ".xml", false);
	else
		snprintf (xdest, sizeof (xdest), "%s.xml", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE NUMATB XML:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, xdest);

	File_t XF;
	err = CreateFileOpt (&XF, true, xdest, false, arg);
	if (XF.f)
		err = DecodeNUMATB_XML (XF.f, raw, raw_size);
	const enumError xml_close_err = ResetFile (&XF, opt_preserve);
	if (xml_close_err > err)
		err = xml_close_err;
	FREE (raw);
	return err;
}
