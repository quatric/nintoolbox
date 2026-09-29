// "Bj" engine (Super Karts, Wii) .tx1 headers + sibling .tx2 pixels -> one
// PNG per texture (name_0.png, name_1.png, ...).
static enumError extract_bj_tx_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".tx1"))
		return ERR_NOTHING_TO_DO;
	u8 *t1 = 0, *t2 = 0;
	size_t s1 = 0, s2 = 0;
	if (LoadFileAlloc (arg, 0, 0, &t1, &s1, 0, 0, 0, false) || !t1)
		return ERR_NOTHING_TO_DO;
	const uint count = IsBjTx1 (t1, s1) ? BjTx1Count (t1, s1) : 0;
	char tx2[PATH_MAX];
	snprintf (tx2, sizeof (tx2), "%.*s.tx2", (int)(strlen (arg) - 4), arg);
	if (!count || LoadFileAlloc (tx2, 0, 0, &t2, &s2, 0, 0, 0, false) || !t2)
	{
		FREE (t1);
		return ERR_NOTHING_TO_DO;
	}
	enumError result = ERR_OK;
	for (uint i = 0; i < count && !result; i++)
	{
		char ext[24], dest[PATH_MAX];
		snprintf (ext, sizeof (ext), "_%u.png", i);
		beside_source_dest_ext (dest, sizeof (dest), arg, ext);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT BJ-TEX:%s -> PNG:%s\n", arg, dest);
			continue;
		}
		if (blitz_output_current (arg, dest))
			continue;
		u8 *rgba = 0;
		uint w = 0, h = 0;
		if (DecodeBjTexture (&rgba, &w, &h, t1, s1, t2, s2, i))
			continue;
		const enumError err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, dest, 0, true);
		if (err)
			result = err;
		else if (verbose >= 0)
			fprintf (stdlog, "%sEXTRACT BJ-TEX:%s -> PNG:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	}
	FREE (t1);
	FREE (t2);
	return result;
}
