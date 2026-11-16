// Rubik's Puzzle World (Wii) ".etx" textures, big-endian: {u16 width, u16 height, u32 format << 16} (0x10 = GX RGBA8, 0x12 = RGB565, 0x15 = I8), the GX tiled pixel data
// (width * height * bytes per pixel) and a 24 byte zero trailer. Written as PNG; other formats are not handled.
static enumError extract_etx_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".etx"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *rgba = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x30)
		goto done;
	{
		const uint w = rd_be16 (raw), h = rd_be16 (raw + 2), fmtw = rd_be32 (raw + 4);
		uint gx = 99, bpp = 0, tw = 4, th = 4;
		if (fmtw == 0x100000)
			gx = 6, bpp = 4;
		else if (fmtw == 0x120000)
			gx = 4, bpp = 2;
		else if (fmtw == 0x150000)
			gx = 1, bpp = 1, tw = 8;
		if (gx == 99 || !w || !h || w > 4096 || h > 4096 || w % tw || h % th || 8 + (u64)w * h * bpp + 24 != sz)
			goto done;
		if (DecodeGXTexture_RGBA (&rgba, w, h, gx, raw + 8, w * h * bpp, 0, 0, 0) || !rgba)
			goto done;
		char name[PATH_MAX];
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			*dot = 0;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE ETX:%s (%ux%u fmt %u) -> PNG\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, gx);
		err = testmode ? ERR_OK : kyl_write_rgba_png (arg, basedir, name, (uint)-1, w, h, rgba);
	}
done:
	FREE (rgba);
	FREE (raw);
	return err;
}
