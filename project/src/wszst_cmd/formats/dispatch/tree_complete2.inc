	// Extension-only pass-through claims (.nds/.cia/.3ds/.cci/.cxi/
	// .wad/.app) run only after every native probe declined the file, so
	// a container misnamed as, say, a WAD never masks a real SZS archive.
	if (!opt_no_passthrough)
	{
		char staged_dir[PATH_MAX] = "";
		enumError pas_err = PassthruExtract (arg, pbase, staged_dir, sizeof (staged_dir));
		if (pas_err != ERR_NOTHING_TO_DO)
			return pas_err == ERR_OK && *staged_dir ? extract_tree_complete (staged_dir, depth + 1)
													: pas_err;
	}
