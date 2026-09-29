//
///////////////////////////////////////////////////////////////////////////////
///////////////			command <FFORM>			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_FFORM (file_format_t fform, ccp autoname)
{
	stdlog = stderr;
	// char pathbuf[PATH_MAX];
	enable_kcl_drop_auto++;

	enumError max_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT_SUB);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		szs_extract_t eszs;
		enumError err = ExtractSZS (&eszs, true, arg, autoname, opt_ignore > 0);

		// [[analyse-magic]]
		if (eszs.data && GetByMagicFF (eszs.data, eszs.data_size, eszs.data_size) == fform)
		{
			if (verbose > 0 || testmode)
				fprintf (stdlog, "%s%sCAT %s:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", GetNameFF (eszs.fform_file, eszs.fform_arch),
					eszs.fname);

			if (!testmode)
				ConvertHelper (eszs.data, eszs.data_size, LinkContainerESZS (&eszs), eszs.fname,
					"-", false, &err);
		}
		else if (!err && !opt_ignore)
			err = PrintErrorExtractSZS (&eszs, arg);

		if (max_err < err)
			max_err = err;
		ResetExtractSZS (&eszs);
	}

	ResetStringField (&plist);
	return max_err;
}
