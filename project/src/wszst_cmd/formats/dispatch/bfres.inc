	// Covers a standalone .bfres passed directly as a single argument (not
	// reached by extract_bfres_textures_tree()'s pre-pass, which only walks
	// directory/archive-shaped extractions). Always returns ERR_NOTHING_TO_DO
	// -- see its own comment for why it must not short-circuit this chain.
	extract_bfres_textures (arg, basedir, depth);
