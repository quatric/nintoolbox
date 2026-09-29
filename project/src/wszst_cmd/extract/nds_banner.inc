// A Nintendo DS banner ("banner.bin", as ndstool -b writes it) carries more
// than the one icon AssignIMG() can return: up to 8 per-language titles, and
// on DSi-enhanced titles an 8-frame animated icon with its own bitmaps and
// palettes.  Write those out beside the file -- a text sidecar with the
// titles and the animation sequence, plus one PNG per animation frame.  The
// static icon itself is left to the normal image path.
static enumError extract_nds_banner_metadata (ccp arg)
{
	u8 *data = 0;
	size_t file_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &data, &file_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (!IsNDSBanner (data, file_size))
	{
		FREE (data);
		return ERR_NOTHING_TO_DO;
	}

	nds_banner_t banner;
	if (ScanNDSBanner (&banner, data, file_size))
	{
		FREE (data);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NDS banner titles:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	enumError err = ERR_OK;
	if (!testmode)
	{
		char *text = TextNDSBanner (&banner);
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f)
		{
			fputs (text, F.f);
			ResetFile (&F, 0);
		}
		FREE (text);
	}

	// Static banner icon
	char static_png[PATH_MAX];
	if (opt_dest)
		SubstDest (static_png, sizeof (static_png), arg, opt_dest, 0, ".png", false);
	else
		snprintf (static_png, sizeof (static_png), "%s.png", arg);

	if (opt_overwrite || access (static_png, F_OK))
	{
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%sEXTRACT NDS static banner icon:%s -> %s\n",
				testmode ? "WOULD " : "", arg, static_png);
		if (!testmode)
		{
			u8 *rgba = 0;
			uint w = 0, h = 0;
			if (!DecodeNDSBannerIcon_RGBA (&rgba, &w, &h, &banner, NDS_BANNER_ICON_STATIC))
				err = SaveDecodedRGBAToPNG (rgba, w, h, &le_func, static_png, 0, opt_overwrite);
		}
	}

	// Animated icon, if any.  A 64-step sequence typically cycles through the
	// same handful of images, so write one PNG per distinct bitmap+palette+
	// flip combination rather than one per step: the file name spells out
	// that combination, which is exactly what the text sidecar's sequence
	// listing names for each step, so the two line up without a manifest.
	bool done[NDS_BANNER_DSI_FRAMES][NDS_BANNER_DSI_FRAMES][4];
	memset (done, 0, sizeof (done));
	for (uint i = 0; !err && i < banner.n_frames; i++)
	{
		const nds_banner_frame_t *fr = banner.frame + i;
		const uint flip = (fr->flip_h ? 1 : 0) | (fr->flip_v ? 2 : 0);
		if (done[fr->bitmap][fr->palette][flip])
			continue;
		done[fr->bitmap][fr->palette][flip] = true;

		char png[PATH_MAX], suffix[40];
		snprintf (suffix, sizeof (suffix), ".anim-b%u-p%u%s%s.png", fr->bitmap, fr->palette,
			fr->flip_h ? "-h" : "", fr->flip_v ? "-v" : "");
		if (opt_dest)
			SubstDest (png, sizeof (png), arg, opt_dest, 0, suffix, false);
		else
			snprintf (png, sizeof (png), "%s%s", arg, suffix);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%sEXTRACT NDS animated banner icon:%s -> %s\n",
				testmode ? "WOULD " : "", arg, png);
		if (testmode)
			continue;

		u8 *rgba = 0;
		uint w = 0, h = 0;
		if (DecodeNDSBannerIcon_RGBA (&rgba, &w, &h, &banner, i))
			break;
		err = SaveDecodedRGBAToPNG (rgba, w, h, &le_func, png, 0, opt_overwrite);
	}

	ResetNDSBanner (&banner);
	FREE (data);
	return err;
}
