static enumError extract_nsmbw_chk_file (ccp arg, ccp basedir, uint depth)
{
	if (!strstr (arg, "d_bgchk_") && !strstr (arg, "_chk.bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsNSMBWChk (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nsmbw_tileset_t ts;
	memset (&ts, 0, sizeof (ts));
	memcpy (ts.beh, raw, 2048);
	ts.have_beh = true;
	FREE (raw);

	char *txt = 0;
	size_t txt_len = 0;
	err = DumpNSMBWBehaviour (&ts, &txt, &txt_len, FindFilename (arg, 0));
	if (err || !txt)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, ".txt", 0, false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NSMBW-CHK (Collision Behaviour):%s -> %s\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f && fwrite (txt, 1, txt_len, F.f) != txt_len)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing Behaviour text failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}

	FREE (txt);
	return err;
}
