// Capcom ".apx" 24 bit images (Zack & Wiki, Wii: "Demo/Logo/Title_*.apx"), little endian: {u32 file size, u32 pixel bytes, u32 palette bytes, u16 bits per
// pixel, u16 width, u16 height, u16 1}, padded to 0x20, then width * height * 3 linear RGB bytes (top row first). Only the 24 bit files (the title screens)
// are decoded to "<name>.png"; the 4 / 8 bit files (logos, system fonts) carry palettes whose layout is not understood and are left alone.
static enumError extract_apx_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".apx"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *rgba = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 0x20 && rd_le32 (raw) == sz && rd_le16 (raw + 12) == 24 && !rd_le32 (raw + 8) && rd_le16 (raw + 18) == 1)
	{
		const uint w = rd_le16 (raw + 14), h = rd_le16 (raw + 16);
		if (w && h && rd_le32 (raw + 4) == (u32)w * h * 3 && 0x20 + (u64)w * h * 3 == sz && (rgba = MALLOC ((size_t)w * h * 4)))
		{
			for (size_t i = 0; i < (size_t)w * h; i++)
			{
				memcpy (rgba + i * 4, raw + 0x20 + i * 3, 3);
				rgba[i * 4 + 3] = 255;
			}
			char name[PATH_MAX];
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot)
				*dot = 0;
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE APX:%s (%ux%u 24 bit) -> PNG\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h);
			err = testmode ? ERR_OK : kyl_write_rgba_png (arg, basedir, name, (uint)-1, w, h, rgba);
		}
	}
	FREE (rgba);
	FREE (raw);
	return err;
}
