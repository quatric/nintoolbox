static enumError extract_nsmbw_tex_file (ccp arg, ccp basedir, uint depth)
{
	if (!strstr (arg, "_tex.bin") && !strstr (arg, "_tex.bin.LZ") && !strstr (arg, "_tex.bin.LH"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	u8 *decoded = NULL;
	uint decoded_size = 0;

	if (raw_size == 524288)
	{
		decoded = raw;
		decoded_size = (uint)raw_size;
		raw = NULL;
	}
	else if (raw_size > 4 && (raw[0] == 0x11 || raw[0] == 0x10 || raw[0] == 0x40))
	{
		if (raw[0] == 0x40)
			err = DecodeLZH8 (&decoded, &decoded_size, raw, raw_size);
		else
			err = DecodeLZ10LZ11 (&decoded, &decoded_size, raw, raw_size);
		FREE (raw);
		if (err || decoded_size != 524288 || !decoded)
		{
			FREE (decoded);
			return ERR_NOTHING_TO_DO;
		}
	}
	else
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *rgba = MALLOC (1024 * 256 * 4);
	if (!rgba)
	{
		FREE (decoded);
		return ERR_OUT_OF_MEMORY;
	}
	DecodeRGB4A3 (rgba, decoded, decoded_size);
	FREE (decoded);

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, ".png", 0, false);
	else
	{
		snprintf (dest, sizeof (dest), "%s", arg);
		char *ext = strstr (dest, "_tex.bin");
		if (ext)
			snprintf (ext, sizeof (dest) - (ext - dest), "_tex.png");
		else
			snprintf (dest + strlen (dest), sizeof (dest) - strlen (dest), ".png");
	}

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NSMBW-TEX (1024x256 RGB5A3):%s -> %s\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
	{
		Image_t img;
		InitializeIMG (&img);
		img.data = rgba;
		img.data_alloced = true;
		img.data_size = 1024 * 256 * 4;
		img.width = 1024;
		img.xwidth = 1024;
		img.height = 256;
		img.xheight = 256;
		img.iform = img.info_iform = IMG_X_RGB;
		img.info_fform = FF_PNG;
		img.info_n_image = 1;
		img.endian = &le_func;

		err = SavePNG (&img, false, 0, dest, 0, 0, opt_overwrite > 0, 0);
		ResetIMG (&img);
	}
	else
	{
		FREE (rgba);
	}

	return err;
}
