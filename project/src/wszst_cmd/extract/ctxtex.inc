// Bionicle Heroes (Eurocom, Wii) "*.CTX" textures, big-endian: {u32 0, u16 width, u16 height, u16 GX format (0 I4, 1 I8, 2 IA4, 3 IA8, 4 RGB565, 5 RGB5A3, 6 RGBA8, 14 CMPR), u16 levels, u32 0x34, ...}, GX pixel data from 0x40.
// The first level is written to "<file>.png". Other ".ctx" files (text tables) are left to their own extractors.
static enumError extract_ctxtex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".ctx"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x50)
		goto done;
	{
		if (rd_be32 (raw) || rd_be32 (raw + 12) != 0x34)
			goto done;
		const uint w = rd_be16 (raw + 4), h = rd_be16 (raw + 6), fmt = rd_be16 (raw + 8), lv = rd_be16 (raw + 10);
		static const uint bits[15] = { 4, 8, 8, 16, 16, 16, 32, 0, 0, 0, 0, 0, 0, 0, 4 };
		if (!w || !h || w > 4096 || h > 4096 || fmt > 14 || !bits[fmt] || !lv || lv > 16)
			goto done;
		const uint bw = fmt == 0 || fmt == 14 ? 8 : fmt == 1 || fmt == 2 ? 8 : 4, bh = fmt == 0 || fmt == 14 ? 8 : 4;
		const u64 l0 = (u64)((w + bw - 1) / bw) * bw * ((h + bh - 1) / bh) * bh * bits[fmt] / 8;
		if (0x40 + l0 > sz)
			goto done;
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.png", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE CTX:%s (%ux%u fmt %u) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, fmt, png);
		err = testmode ? ERR_OK : wiitex_write_raw (png, w, h, fmt, raw + 0x40, (uint)l0);
	}
done:
	FREE (raw);
	return err;
}
