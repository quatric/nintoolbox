// Jurassic: The Hunted (Wii) "Gui/Textures/**/*.wit" textures, little-endian: {u32 width, u32 height, u32 GX format (0 I4, 1 I8, 2 IA4, 3 IA8, 4 RGB565, 5 RGB5A3, 6 RGBA8, 14 CMPR), u32 levels}, GX pixel data from 0x10.
// The first level is written to "<file>.png".
static enumError extract_wittex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".wit"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x20)
		goto done;
	{
		const uint w = rd_le32 (raw), h = rd_le32 (raw + 4), fmt = rd_le32 (raw + 8), lv = rd_le32 (raw + 12);
		static const uint bits[15] = { 4, 8, 8, 16, 16, 16, 32, 0, 0, 0, 0, 0, 0, 0, 4 };
		if (!w || !h || w > 4096 || h > 4096 || fmt > 14 || !bits[fmt] || !lv || lv > 16)
			goto done;
		const uint bw = fmt == 0 || fmt == 14 ? 8 : fmt == 1 || fmt == 2 ? 8 : 4, bh = fmt == 0 || fmt == 14 ? 8 : 4;
		const u64 l0 = (u64)((w + bw - 1) / bw) * bw * ((h + bh - 1) / bh) * bh * bits[fmt] / 8;
		if (0x10 + l0 > sz)
			goto done;
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.png", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE WIT:%s (%ux%u fmt %u) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, fmt, png);
		err = testmode ? ERR_OK : wiitex_write_raw (png, w, h, fmt, raw + 0x10, (uint)l0);
	}
done:
	FREE (raw);
	return err;
}
