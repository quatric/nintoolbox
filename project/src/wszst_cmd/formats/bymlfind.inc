//
///////////////////////////////////////////////////////////////////////////////
///////////////			command bymlfind		///////////////
///////////////////////////////////////////////////////////////////////////////
//
// Case-insensitive substring search over BYML keys and values, the CLI
// counterpart of NintenTools.Byaml's editor search. Plain and Yaz0-wrapped
// (BotW .sbyml) inputs are both accepted.

static enumError cmd_bymlfind ()
{
	CheckOptDest ("-", false);

	if (!first_param || !first_param->arg || !*first_param->arg)
		return ERROR0 (
			ERR_SYNTAX, "BYMLFIND needs a search pattern: wszst BYMLFIND pattern [source]...\n");
	ccp pattern = first_param->arg;

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param->next, -1, WM__DEFAULT);
	if (!plist.used)
	{
		ResetStringField (&plist);
		return ERROR0 (ERR_SYNTAX, "BYMLFIND needs at least one source file.\n");
	}

	enumError max_err = ERR_OK;
	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		u8 *data = 0;
		size_t size = 0;
		enumError err = LoadBYMLData (arg, &data, &size);
		if (err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore)
		{
			FREE (data);
			continue;
		}
		if (err)
		{
			FREE (data);
			// Print before ResetStringField(): 'arg' points into 'plist'.
			ERROR0 (err, "Not a BYML file: %s\n", arg);
			ResetStringField (&plist);
			return err;
		}

		char dest[PATH_MAX];
		SubstDest (dest, sizeof (dest), arg, opt_dest, ".txt", ".txt", false);

		uint found = 0;
		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (!F.f)
				err = ERR_WRITE_FAILED;
			else
			{
				err = SearchBYML (F.f, data, size, pattern, &found);
				ResetFile (&F, opt_preserve);
			}
		}
		else
		{
			// Test mode: count matches without writing anything.
			err = SearchBYML (0, data, size, pattern, &found);
		}
		FREE (data);
		if (err)
		{
			ResetStringField (&plist);
			return err;
		}
		if (verbose >= 0)
		{
			fprintf (stdlog, "%sBYMLFIND '%s' %s: %u match%s%s\n", verbose > 0 ? "\n" : "", pattern,
				arg, found, found == 1 ? "" : "es", testmode ? " (test mode)" : "");
			fflush (stdlog);
		}
	}

	ResetStringField (&plist);
	return max_err;
}
