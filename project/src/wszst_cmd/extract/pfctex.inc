// Decode a Nintendo Pocket Football Club "CALCIO3DS" UI texture (.bin) to a
// sibling PNG. No text magic -- IsPFCTex()/DecodePFCTex_RGBA() validate the
// header structurally, so this only fires on ".bin" files that actually
// match; every other ".bin"-gated extractor in this file returns
// ERR_NOTHING_TO_DO first on a mismatch, same as here. See lib-pfctex.h.
static enumError extract_pfctex_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX || !IsPFCTex (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *rgba = 0;
	uint w = 0, h = 0;
	enumError err = DecodePFCTex_RGBA (&rgba, &w, &h, raw, (uint)raw_size);
	FREE (raw);
	if (err || !rgba)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_image_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PFCTEX:%s (%ux%u) -> PNG:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, w, h, dest);

	if (testmode)
	{
		FREE (rgba);
		return ERR_OK;
	}
	return SaveDecodedRGBAToPNG (rgba, w, h, &le_func, dest, 0, opt_overwrite);
}
