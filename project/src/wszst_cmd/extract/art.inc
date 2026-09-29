// Extract a Monster Games .art/.img GUI image to a sibling PNG. Same GX
// pixel data as .tex but a single mip level and a zeroed footer (see
// ScanART), so dimensions/format come from tile-seam continuity alone.
// Colour+stencil pairs (see README) are detected and recombined into one
// proper RGBA image by ScanART itself, so this function always just writes
// out whatever it returns.
static enumError extract_art_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".art") && !is_ext (arg, ".img"))
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
	err = ScanART (&tex, raw, (uint)raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_image_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ART:%s (%ux%u) -> PNG:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, tex.width, tex.height, dest);

	if (testmode)
		ResetExciteTEX (&tex);
	else
	{
		u8 *rgba = tex.rgba;
		tex.rgba = 0;
		err = SaveDecodedRGBAToPNG (rgba, tex.width, tex.height, &le_func, dest, 0, opt_overwrite);
		ResetExciteTEX (&tex);
	}
	return err;
}
