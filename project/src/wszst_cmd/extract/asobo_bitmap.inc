// Asobo Bitmap_Z texture (Ratatouille .DRV member) -> PNG.
static enumError extract_asobo_bitmap_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bitmap_z"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX || !IsAsoboBitmap (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".png");
	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT ASOBO-BITMAP:%s -> PNG:%s\n", arg, dest);
		return ERR_OK;
	}
	if (blitz_output_current (arg, dest))
	{
		FREE (raw);
		return ERR_OK;
	}
	u8 *rgba = 0;
	uint w = 0, h = 0;
	err = DecodeAsoboBitmap (&rgba, &w, &h, raw, (uint)raw_size);
	FREE (raw);
	if (err)
		return err;
	err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, dest, 0, true);
	if (!err && verbose >= 0)
		fprintf (
			stdlog, "%sEXTRACT ASOBO-BITMAP:%s -> PNG:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
