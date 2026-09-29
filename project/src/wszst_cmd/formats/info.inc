//
///////////////////////////////////////////////////////////////////////////////
///////////////			command info			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_info ()
{
	stdlog = stderr;

	char pathbuf[PATH_MAX];
	ccp search_tab[] = { "info.txt", "credits.txt", 0 };

	szs_extract_t eszs;
	InitializeExtractSZS (&eszs);

	enumError max_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		ccp *search;
		for (search = search_tab;; search++)
		{
			if (!*search)
			{
				if (!opt_ignore)
					printf ("\n* NO INFO FOUND: %s\n\n", arg);
				max_err = ERR_WARNING;
				break;
			}

			ccp path = PathCatPP (pathbuf, sizeof (pathbuf), arg, *search);
			ResetExtractSZS (&eszs);
			enumError err = ExtractSZS (&eszs, false, path, 0, true);
			if (!err && eszs.data)
			{
				if (print_header)
					printf ("\n* %s\n\n", eszs.fname);
				fwrite (eszs.data, 1, eszs.data_size, stdout);
				if (eszs.data_size && eszs.data[eszs.data_size - 1] != '\n')
					putchar ('\n');
				if (print_header)
					putchar ('\n');
				break;
			}
		}
	}

	ResetStringField (&plist);
	ResetExtractSZS (&eszs);
	return max_err;
}
