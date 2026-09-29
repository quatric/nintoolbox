static enumError extract_mii_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err || raw_size < 40 || raw_size > 4096)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	if (!IsMiiData (raw, (uint)raw_size, arg))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	if (!opt_export_miis)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest_png[PATH_MAX];
	char dest_glb[PATH_MAX];
	beside_source_dest_ext (dest_png, sizeof (dest_png), arg, ".png");
	beside_source_dest_ext (dest_glb, sizeof (dest_glb), arg, ".glb");

	u8 *png_data = 0;
	uint png_size = 0;
	u8 *glb_data = 0;
	uint glb_size = 0;

	RenderMiiPNG (&png_data, &png_size, raw, (uint)raw_size, 512);
	RenderMiiGLB (&glb_data, &glb_size, raw, (uint)raw_size);

	if (!png_data && !glb_data)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MII:   %s -> %s%s%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, png_data ? dest_png : "",
			(png_data && glb_data) ? " + " : "", glb_data ? dest_glb : "");

	if (!testmode)
	{
		if (png_data)
		{
			File_t F;
			CreateFileOpt (&F, true, dest_png, false, arg);
			if (F.f && png_size)
				fwrite (png_data, 1, png_size, F.f);
			ResetFile (&F, opt_preserve);
		}
		if (glb_data)
		{
			File_t F;
			CreateFileOpt (&F, true, dest_glb, false, arg);
			if (F.f && glb_size)
				fwrite (glb_data, 1, glb_size, F.f);
			ResetFile (&F, opt_preserve);
		}
	}

	FREE (png_data);
	FREE (glb_data);
	FREE (raw);
	return ERR_OK;
}
