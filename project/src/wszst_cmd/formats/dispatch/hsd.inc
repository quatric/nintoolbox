	// The HAL .dat/.usd probes are different: they DO claim the file. Those
	// archives are not directory-style containers, so once identified there is
	// nothing left below for the chain to do -- and the generic SZS/U8
	// extractor at the end would only fail them with ERR_FILE_ALREADY_EXISTS.
	err = extract_hsd_textures (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
