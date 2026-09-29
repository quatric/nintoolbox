	// Last of the archive scanners: Metroid: Samus Returns has no magic at
	// all, so it only gets a look after every signature-bearing format has
	// declined the file.
	err = extract_metroidsr_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
