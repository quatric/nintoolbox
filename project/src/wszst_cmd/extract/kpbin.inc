static enumError extract_kpbin_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsKPBin (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	kpbin_t kp;
	err = ScanKPBin (&kp, raw, raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char *txt = 0;
	size_t txt_len = 0;
	err = DumpKPBinText (&kp, &txt, &txt_len);
	if (err || !txt)
	{
		ResetKPBin (&kp);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, ".txt", 0, false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT KPBIN (Koopatlas Map):%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f && fwrite (txt, 1, txt_len, F.f) != txt_len)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing Koopatlas map text failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}

	FREE (txt);

	// Also write JSON representation
	char *json = 0;
	size_t json_len = 0;
	if (DumpKPBinJson (&kp, &json, &json_len) == ERR_OK && json)
	{
		char jdest[PATH_MAX];
		if (opt_dest && (strstr (opt_dest, ".json") || strstr (opt_dest, ".kpmap")))
		{
			SubstDest (jdest, sizeof (jdest), arg, opt_dest, 0, 0, false);
		}
		else
		{
			snprintf (jdest, sizeof (jdest), "%s", arg);
			char *dot = strrchr (jdest, '.');
			if (dot)
				snprintf (dot, sizeof (jdest) - (dot - jdest), ".json");
			else
				snprintf (jdest + strlen (jdest), sizeof (jdest) - strlen (jdest), ".json");
		}

		if (!testmode)
		{
			File_t Fj;
			if (CreateFileOpt (&Fj, true, jdest, false, arg) == ERR_OK)
			{
				if (Fj.f)
					fwrite (json, 1, json_len, Fj.f);
				ResetFile (&Fj, opt_preserve);
			}
		}
		FREE (json);
	}

	ResetKPBin (&kp);
	return err;
}
