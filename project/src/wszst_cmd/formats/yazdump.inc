//
///////////////////////////////////////////////////////////////////////////////
///////////////			command decompress		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_yazdump ()
{
	CheckOptDest ("-", false);

	enumError max_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		szs_file_t szs;
		InitializeSZS (&szs);
		enumError err = LoadSZS (&szs, arg, false, opt_ignore > 0, false);

		if (err <= ERR_WARNING && err != ERR_NOT_EXISTS
			&& (szs.fform_file == FF_YAZ0 || szs.fform_file == FF_YAZ1))
		{
			char dest[PATH_MAX];
			SubstDest (dest, sizeof (dest), arg, opt_dest, ".txt", ".txt", false);

			File_t F;
			CreateFileOpt (&F, true, dest, testmode, 0);
			if (F.f)
			{
				if (verbose >= 0 || testmode)
					fprintf (F.f, "%sYAZ DUMP of %s:%s\n", verbose > 0 ? "\n" : "",
						GetNameFF_SZS (&szs), arg);
				err = DecompressSZS (&szs, true, F.f);
			}
		}

		if (max_err < err)
			max_err = err;
		ResetSZS (&szs);
	}

	ResetStringField (&plist);
	return max_err;
}
