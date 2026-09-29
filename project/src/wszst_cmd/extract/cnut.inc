static enumError extract_cnut_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".cnut") && !is_ext (arg, ".nut") && !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsCNUT (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	cnut_t cnut;
	err = ScanCNUT (&cnut, raw, raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char *disasm = 0;
	size_t disasm_len = 0;
	err = DisassembleCNUT (&cnut, &disasm, &disasm_len);
	if (err || !disasm)
	{
		ResetCNUT (&cnut);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, ".nut", 0, false);
	else
		snprintf (dest, sizeof (dest), "%s.nut", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CNUT (Wii Party):%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (testmode)
	{
		FREE (disasm);
		ResetCNUT (&cnut);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f && fwrite (disasm, 1, disasm_len, F.f) != disasm_len)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing disassembly failed: %s\n", dest);
	ResetFile (&F, opt_preserve);
	FREE (disasm);

	// Also extract strings table if there are string literals
	char *strings_txt = 0;
	size_t str_len = 0;
	if (ExtractCNUTStrings (&cnut, &strings_txt, &str_len) == ERR_OK && strings_txt && str_len > 0)
	{
		char str_dest[PATH_MAX];
		snprintf (str_dest, sizeof (str_dest), "%s", dest);
		char *ext = strrchr (str_dest, '.');
		if (ext && !strcmp (ext, ".nut"))
			snprintf (ext, sizeof (str_dest) - (ext - str_dest), ".txt");
		else
			snprintf (str_dest + strlen (str_dest), sizeof (str_dest) - strlen (str_dest), ".txt");

		File_t F_str;
		if (CreateFileOpt (&F_str, true, str_dest, false, arg) == ERR_OK)
		{
			if (F_str.f)
				fwrite (strings_txt, 1, str_len, F_str.f);
			ResetFile (&F_str, opt_preserve);
		}
		FREE (strings_txt);
	}

	ResetCNUT (&cnut);
	return err;
}
