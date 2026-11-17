// Jurassic: The Hunted and Cabela's (Wii, Fun Labs) "*.wit" textures (Cabela's: u32 name hash first, data at 0x18), little-endian: {u32 width, u32 height, u32 GX format (0 I4, 1 I8, 2 IA4, 3 IA8, 4 RGB565, 5 RGB5A3, 6 RGBA8, 14 CMPR), u32 levels}, GX pixel data from 0x10.
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
	for (uint hb = 0; hb < 8 && err == ERR_NOTHING_TO_DO; hb += 4) // hb 4: Cabela's variant with a leading name hash and data at 0x18
	{
		uint w = rd_le32 (raw + hb), h = rd_le32 (raw + hb + 4), fmt = rd_le32 (raw + hb + 8), lv = rd_le32 (raw + hb + 12);
		if (hb && fmt == 0x800) // Cabela's: two stacked CMPR mip chains (the second one is not decoded); the level count is stored twice
		{
			fmt = 14;
			lv &= 0xffff;
		}
		static const uint bits[15] = { 4, 8, 8, 16, 16, 16, 32, 0, 0, 0, 0, 0, 0, 0, 4 };
		if (!w || !h || w > 4096 || h > 4096 || fmt > 14 || !bits[fmt] || !lv || lv > 16)
			continue;
		const uint bw = fmt == 0 || fmt == 14 ? 8 : fmt == 1 || fmt == 2 ? 8 : 4, bh = fmt == 0 || fmt == 14 ? 8 : 4;
		const u64 l0 = (u64)((w + bw - 1) / bw) * bw * ((h + bh - 1) / bh) * bh * bits[fmt] / 8;
		const uint doff = hb ? 0x18 : 0x10;
		if (doff + l0 > sz)
			continue;
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.png", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE WIT:%s (%ux%u fmt %u) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, fmt, png);
		err = testmode ? ERR_OK : wiitex_write_raw (png, w, h, fmt, raw + doff, (uint)l0);
	}
done:
	FREE (raw);
	return err;
}
