// Infinity Ward ".iwi" images, version 11 (Call of Duty: Black Ops, Wii): {"IWi", u8 version 11, u8 format 11 (GX CMPR / DXT1), u8 flags, u16 LE width, u16 LE height, u16 depth, u32 offsets[5], ...} with the
// 0x20-byte header followed by the pixel data in GX CMPR tile layout. Only files holding exactly one CMPR level are decoded, to "<file>.png".
static enumError extract_iwiimg_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".iwi"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 0x28 && !memcmp (raw, "IWi", 3) && raw[3] == 11 && raw[4] == 11)
	{
		const uint w = rd_le16 (raw + 6), h = rd_le16 (raw + 8);
		if (w && h && w <= 4096 && h <= 4096 && sz - 0x20 == (size_t)((w + 7) / 8 * 8) * ((h + 7) / 8 * 8) / 2)
		{
			char png[PATH_MAX];
			snprintf (png, sizeof (png), "%s.png", arg);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE IWI:%s (%ux%u CMPR) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, png);
			err = testmode ? ERR_OK : wiitex_write_raw (png, w, h, 14, raw + 0x20, (uint)(sz - 0x20));
		}
	}
	FREE (raw);
	return err;
}
