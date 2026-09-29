// magic 0x80371240, not romc's "size-in-4MB-units" header).
static enumError decode_romc_if_possible (ccp arg)
{
	const ccp base = strrchr (arg, '/');
	if (strcmp (base ? base + 1 : arg, "romc"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	u8 *dest = 0;
	uint dest_size = 0;
	err = DecodeRomC (&dest, &dest_size, raw, (uint)raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO; // e.g. the undiscovered romchu/Huffman variant

	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s.z64", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE romc:%s -> Z64:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, path);
	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && dest_size && fwrite (dest, 1, dest_size, F.f) != dest_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", dest_size, path);
		ResetFile (&F, opt_preserve);
	}
	FREE (dest);
	return err;
}

// Extract a Monster Games .tex GX texture (Excite Truck / ExciteBots) to a
// sibling PNG. No stored pixel format -- ScanTEX() recovers it (and the
// dimensions) via mip-chain-consistency scoring; see lib-excite.c.
static enumError extract_tex_file (ccp arg, ccp basedir, uint depth)
{
	const bool is_tm0 = is_ext (arg, ".tm0");
	if (!is_tm0 && !is_ext (arg, ".tex"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}

	excite_tex_t tex;
	err = is_tm0 ? ScanTM0 (&tex, raw, (uint)raw_size) : ScanTEX (&tex, raw, (uint)raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_image_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT %s:%s (%ux%u) -> PNG:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", is_tm0 ? "TM0" : "TEX", arg, tex.width, tex.height, dest);

	if (testmode)
		ResetExciteTEX (&tex);
	else
	{
		u8 *rgba = tex.rgba;
		tex.rgba = 0; // ownership transfers to SaveDecodedRGBAToPNG
		err = SaveDecodedRGBAToPNG (rgba, tex.width, tex.height, &le_func, dest, 0, opt_overwrite);
		ResetExciteTEX (&tex);
	}
	return err;
}
