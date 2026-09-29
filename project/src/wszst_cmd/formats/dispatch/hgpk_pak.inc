	// Banpresto/Now Production tagged resource table (Crayon Shin-chan:
	// Saikyou Kazoku Kasukabe King, Wii) -- see lib note in
	// extract_hgpk_pak_file(). Reached directly as a disc ".pak" file, and
	// as the ".hgpk" file decompress_nintendo_file3()'s MTXZ branch (below)
	// produces from a zlib-wrapped ".mz" asset.
	err = extract_bermuda_pki_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_hgpk_pak_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
