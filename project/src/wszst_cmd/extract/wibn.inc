// Wii save banner (WIBN): write the title/subtitle sidecar and one PNG per
// icon animation frame. The 192x64 banner image itself is an ordinary wimgt
// input (AssignIMG claims WIBN), so it is not duplicated here.
static enumError extract_wibn_metadata (ccp arg)
{
	u8 *data = 0;
	size_t file_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &data, &file_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (!IsWIBN (data, file_size))
	{
		FREE (data);
		return ERR_NOTHING_TO_DO;
	}

	wibn_t wibn;
	if (ScanWIBN (&wibn, data, file_size))
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
		fprintf (stdlog, "%s%sEXTRACT WIBN save banner titles:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	enumError err = ERR_OK;
	if (!testmode)
	{
		char *text = TextWIBN (&wibn);
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f)
		{
			fputs (text, F.f);
			ResetFile (&F, 0);
		}
		FREE (text);
	}

	for (uint i = 0; !err && i < wibn.n_icons; i++)
	{
		char png[PATH_MAX], suffix[40];
		snprintf (suffix, sizeof (suffix), ".icon%u.png", i);
		if (opt_dest)
			SubstDest (png, sizeof (png), arg, opt_dest, 0, suffix, false);
		else
			snprintf (png, sizeof (png), "%s%s", arg, suffix);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%sEXTRACT WIBN save banner icon:%s -> %s\n", testmode ? "WOULD " : "",
				arg, png);
		if (testmode)
			continue;

		u8 *rgba = 0;
		uint w = 0, h = 0;
		if (DecodeWIBNImage_RGBA (&rgba, &w, &h, &wibn, i))
			break;
		err = SaveDecodedRGBAToPNG (rgba, w, h, &le_func, png, 0, opt_overwrite);
	}

	ResetWIBN (&wibn);
	FREE (data);
	return err;
}
