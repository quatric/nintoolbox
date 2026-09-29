static enumError extract_chans_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsChannelScript (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	chans_script_t cs;
	err = ScanChannelScript (&cs, raw, raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char *dump = 0;
	size_t dump_len = 0;
	err = DumpChannelScriptAll (&cs, &dump, &dump_len);
	if (err || !dump)
	{
		ResetChannelScript (&cs);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	char jdest[PATH_MAX];
	if (opt_dest)
	{
		bool is_dir = (opt_dest[strlen (opt_dest) - 1] == '/') || IsDirectory (opt_dest, 0);
		if (is_dir)
		{
			SubstDest (dest, sizeof (dest), arg, opt_dest, "%N.txt", ".txt", false);
			SubstDest (jdest, sizeof (jdest), arg, opt_dest, "%N.js", ".js", false);
		}
		else
		{
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);
			size_t dlen = strlen (dest);
			if (dlen > 4 && !strcasecmp (dest + dlen - 4, ".txt"))
				snprintf (jdest, sizeof (jdest), "%.*s.js", (int)(dlen - 4), dest);
			else if (dlen > 3 && !strcasecmp (dest + dlen - 3, ".js"))
			{
				snprintf (jdest, sizeof (jdest), "%s", dest);
				snprintf (dest, sizeof (dest), "%.*s.txt", (int)(dlen - 3), jdest);
			}
			else
				snprintf (jdest, sizeof (jdest), "%s.js", dest);
		}
	}
	else
	{
		snprintf (dest, sizeof (dest), "%s.txt", arg);
		snprintf (jdest, sizeof (jdest), "%s.js", arg);
	}

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CHANS (ChannelScript):%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f && fwrite (dump, 1, dump_len, F.f) != dump_len)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing ChannelScript dump failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}

	FREE (dump);

	// Also write decompiled script as .js
	char *js = 0;
	size_t js_len = 0;
	if (DumpChannelScriptDecompiled (&cs, &js, &js_len) == ERR_OK && js)
	{
		if (!testmode)
		{
			File_t Fj;
			if (CreateFileOpt (&Fj, true, jdest, false, arg) == ERR_OK)
			{
				if (Fj.f)
					fwrite (js, 1, js_len, Fj.f);
				ResetFile (&Fj, opt_preserve);
			}
		}
		FREE (js);
	}

	ResetChannelScript (&cs);
	return err;
}
