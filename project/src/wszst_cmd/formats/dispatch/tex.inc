	// ExciteBots font textures can begin with 00 01 00 01, which is also a
	// plausible object-flow version/count pair. Extension plus the strict
	// texture-header/size validation must therefore get first refusal before
	// generic format/pass-through probes classify them as OBFLOW.
	err = extract_tex_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
