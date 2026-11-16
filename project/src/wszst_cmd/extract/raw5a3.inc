// Raw RGB5A3 save banner (".5a3", Wii Chess "save/SaveBanner.5a3"): exactly 24,576 bytes = 192 x 64 pixels in GX RGB5A3 4x4 tiles, no header. Written as "<file>.png".
static enumError extract_raw5a3_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".5a3"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz == 192 * 64 * 2)
	{
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.png", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE RAW-5A3:%s (192x64) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, png);
		err = testmode ? ERR_OK : wiitex_write_raw (png, 192, 64, 5, raw, (uint)sz);
	}
	FREE (raw);
	return err;
}
