// 3DS SMDH (icon.bin / *.smdh, standalone or unpacked from a CIA/3DSX/CCI):
// write the per-language title/publisher sidecar. The small/large icons
// themselves are an ordinary wimgt input (AssignIMG claims SMDH), so they
// are not duplicated here.
static enumError extract_smdh_metadata (ccp arg)
{
	u8 *data = 0;
	size_t file_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &data, &file_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (file_size < SMDH_SIZE || memcmp (data, "SMDH", 4))
	{
		FREE (data);
		return ERR_NOTHING_TO_DO;
	}

	smdh_t smdh;
	if (ScanSMDH (&smdh, data, (uint)file_size))
	{
		FREE (data);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SMDH titles:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	enumError err = ERR_OK;
	if (!testmode)
	{
		char *text = TextSMDH (&smdh);
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f)
		{
			fputs (text, F.f);
			ResetFile (&F, 0);
		}
		FREE (text);
	}

	ResetSMDH (&smdh);
	FREE (data);
	return err;
}
