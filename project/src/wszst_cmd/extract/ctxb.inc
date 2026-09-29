// Extract a Grezzo 3DS Texture Container (.ctxb) to a sibling PNG. Unlike
// TPL/CTPK, a loose .ctxb (as shipped directly in a 3DS romfs, not bundled
// inside a GAR/ZAR) never reaches ExtractFilesSZS's per-member decode switch
// or the generic archive-member path, since it isn't a multi-file archive
// at all -- AssignIMG() already decodes its bytes correctly (wimgt DECODE
// uses exactly this path), but nothing called it for a standalone file. The
// decoder only ever recovers the first "tex " chunk's first texture, so this
// always writes exactly one PNG, matching img.info_n_image's hardcoded 1.
static enumError extract_ctxb_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".ctxb"))
		return ERR_NOTHING_TO_DO;

	Image_t img;
	enumError err = LoadIMG (&img, true, arg, 0, false, false, true);
	if (err || img.info_fform != FF_CTXB)
	{
		ResetIMG (&img);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_image_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CTXB:%s (%ux%u) -> PNG:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, img.width, img.height, dest);

	if (!testmode)
		err = SavePNG (&img, false, 0, dest, 0, 0, opt_overwrite, 0);
	ResetIMG (&img);
	return err;
}
