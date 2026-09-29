	// Containers the main tools cannot open natively are passed through
	// to external unpackers (wit, ndstool, ctrtool, sharpii).  The strong
	// variant runs FIRST, claimed by header signature only: it neither
	// reads the whole file (so disc images above --max-file-size reach
	// wit instead of failing the native probes) nor steals any file the
	// native decoders can handle (extension-only claims are left below).
	if (!opt_no_passthrough)
	{
		char staged_dir[PATH_MAX] = "";
		enumError pas_err = PassthruExtractStrong (arg, pbase, staged_dir, sizeof (staged_dir));
		if (pas_err != ERR_NOTHING_TO_DO)
			return pas_err == ERR_OK && *staged_dir ? extract_tree_complete (staged_dir, depth + 1)
													: pas_err;
	}
