//
///////////////////////////////////////////////////////////////////////////////
///////////////			command ghost			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_ghost ()
{
	static ccp def_path = "\1P/\1F\1?T";
	CheckOptDest (def_path, false);
	char dest[PATH_MAX];

	raw_data_t raw;
	InitializeRawData (&raw);

	rkg_info_t ri;
	InitializeRKGInfo (&ri);
	enumError max_err = ERR_OK;

	File_t fo;
	InitializeFile (&fo);

	PrintScript_t ps;
	SetupPrintScriptByOptions (&ps);

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		enumError err = LoadRawData (&raw, false, arg, 0, opt_ignore > 0, 0);
		if (err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore)
			continue;
		if (err > ERR_WARNING)
			return err;

		if (!fo.f)
			SubstDest (
				dest, sizeof (dest), arg, opt_dest, def_path, GetExtFF (script_fform, 0), false);

		if (verbose >= 0)
		{
			fprintf (stdlog, "%sANALYZE %s:%s => %s:%s\n", verbose > 0 ? "\n" : "",
				GetNameFF (raw.fform, 0), raw.fname, GetNameFF (script_fform, 0), dest);
			fflush (stdlog);
		}

		err = ScanRawDataGHOST (&ri, false, &raw);
		if (err >= ERR_WARNING)
		{
			if (max_err < err)
				max_err = err;
			continue;
		}

		if (!fo.f)
		{
			enumError err = CreateFile (&fo, false, dest, FM_STDIO | FM_OVERWRITE);
			if (err)
			{
				max_err = err;
				break;
			}
			ps.f = fo.f;
			PrintScriptHeader (&ps);
		}

		PrintScriptVars (&ps, 3,
			"compressed=%d\n"
			"size=%u\n"
			"score=%u\n"
			"time_button=%u\n"
			"time_direction=%u\n"
			"time_trick=%u\n",
			ri.is_compressed, ri.data_size, ri.score, ri.time_but, ri.time_dir, ri.time_trick);
#if 0
	printf("score = %u, compr = %d, size = %u\n",
		ri.score, ri.is_compressed, ri.data_size );
	printf("but = %u,%u, dir = %u,%u, trick=%u,%u\n",
		ri.n_but, ri.time_but,
		ri.n_dir, ri.time_dir,
		ri.n_trick, ri.time_trick );
#endif

		if (!script_array)
		{
			PrintScriptFooter (&ps);
			ps.f = 0;
			ResetFile (&fo, 0);
		}
	}

	PrintScriptFooter (&ps);

	ResetStringField (&plist);
	ResetPrintScript (&ps);
	ResetFile (&fo, 0);
	ResetRKGInfo (&ri);
	ResetRawData (&raw);
	return max_err;
}
