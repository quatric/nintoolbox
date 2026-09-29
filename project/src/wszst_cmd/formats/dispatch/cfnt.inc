	err = extract_cfnt_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = decode_cfnt_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
