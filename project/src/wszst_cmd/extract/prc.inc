static enumError extract_prc_manifest (ccp arg)
{
	if (!is_ext (arg, ".prc") && !is_ext (arg, ".param"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsPRC (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char *xml = 0;
	err = DecodePRC_XML (&xml, raw, raw_size);
	FREE (raw);
	if (err || !xml)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".xml", false);
	else
		snprintf (dest, sizeof (dest), "%s.xml", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PRC XML:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (testmode)
	{
		FREE (xml);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f && fwrite (xml, 1, strlen (xml), F.f) != strlen (xml))
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing XML failed: %s\n", dest);
	ResetFile (&F, opt_preserve);
	FREE (xml);
	return err;
}
