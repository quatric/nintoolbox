// Decode an ArtePiazza Nintendo DS texture (.tex; Dragon Quest IV/V/VI) to a
// sibling PNG. Validates header structure and dimensions before decoding.
// See lib-artetex.h.
static enumError extract_artetex_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".tex"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX || !IsArteTexture (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *rgba = 0;
	uint w = 0, h = 0;
	enumError err = DecodeArteTexture (&rgba, &w, &h, raw, (uint)raw_size);
	FREE (raw);
	if (err || !rgba)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_image_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ARTE-TEX:%s (%ux%u) -> PNG:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, w, h, dest);

	if (testmode)
	{
		FREE (rgba);
		return ERR_OK;
	}
	return SaveDecodedRGBAToPNG (rgba, w, h, &le_func, dest, 0, opt_overwrite);
}
