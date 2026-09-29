static enumError extract_nwr_animtiles_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsNWRAnimTiles (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nwr_animtiles_t at;
	err = ScanNWRAnimTiles (&at, raw, raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char *txt = 0;
	size_t txt_len = 0;
	err = DumpNWRAnimTilesText (&at, &txt, &txt_len);
	if (err || !txt)
	{
		ResetNWRAnimTiles (&at);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, ".txt", 0, false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NWR-ANIMTILES (Newer SMBW):%s -> %s\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f && fwrite (txt, 1, txt_len, F.f) != txt_len)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing AnimTiles text failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}

	FREE (txt);
	ResetNWRAnimTiles (&at);
	return err;
}
