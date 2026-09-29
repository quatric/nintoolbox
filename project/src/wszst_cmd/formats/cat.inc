//
///////////////////////////////////////////////////////////////////////////////
///////////////			command cat			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_cat ()
{
	stdlog = stderr;
	enumError max_err = ERR_OK;

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT_SUB);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		szs_extract_t eszs;
		enumError err = ExtractSZS (&eszs, true, arg, 0, opt_ignore > 0);
		PRINT ("stat=%u, data=%p, found=%d,%d, path=%s\n", err, eszs.data, !eszs.subfile_found,
			eszs.szs_found, eszs.subpath);

		if (eszs.data)
		{
			if (verbose > 0 || testmode)
			{
				fprintf (stdlog, "%s%sCAT %s:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", GetNameFF (eszs.fform_file, eszs.fform_arch),
					eszs.fname);
				fflush (stdlog);
			}

			if (!testmode)
			{
				if (opt_decode)
				{
					ConvertHelper (eszs.data, eszs.data_size, LinkContainerESZS (&eszs), eszs.fname,
						"-", false, &err);
				}
				else
				{
					size_t wstat = fwrite (eszs.data, 1, eszs.data_size, stdout);
					if (wstat != eszs.data_size)
						err = ERROR1 (ERR_WRITE_FAILED, "Write to stdout failed\n");
				}
			}
		}
		else if (!err && !opt_ignore)
			err = PrintErrorExtractSZS (&eszs, arg);
		fflush (stdout);

		if (max_err < err)
			max_err = err;
		ResetExtractSZS (&eszs);
	}

	ResetStringField (&plist);
	return max_err;
}
