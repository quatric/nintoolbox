static enumError extract_glg_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".glg") && !is_ext (arg, ".rlg"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size < 16 || raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// Outer container tag 0x8001b0{00,01}.
	if (raw[0] != 0x80 || raw[1] != 0x01 || raw[2] != 0xb0 || (raw[3] != 0 && raw[3] != 1))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");

	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT GLG:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}

	const enumError err = DecodeGLG2 (raw, (uint)raw_size, arg, dest);
	FREE (raw);
	if (err)
		return err;

	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT GLG:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
