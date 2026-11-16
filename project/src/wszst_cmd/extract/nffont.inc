// Gamebryo-based bitmap fonts ".nff" (Monster Mayhem, Wii): "NFF\0", u32 LE version (1 - 3), u32 name length + name, a 0x?? byte header with the point size, the glyph code list
// (u16 per glyph, the first one is the replacement glyph) and the glyph cell table, which ends in {u32 width, u32 height, u32 32}; the atlas follows as width * height RGBA8
// pixels (white, alpha = glyph coverage). Written as "<file>.png" (the per-glyph cell table is not decoded).
static enumError extract_nffont_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".nff"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x100 || memcmp (raw, "NFF\0", 4) || rd_le32 (raw + 4) > 3 || !rd_le32 (raw + 4))
		goto done;
	for (uint w = 16; w <= 4096; w *= 2)
		for (uint h = 16; h <= 4096; h *= 2)
		{
			const u64 img = 4ull * w * h;
			if (img + 0x40 > sz)
				continue;
			const size_t hs = sz - (size_t)img;
			if (rd_le32 (raw + hs - 12) != w || rd_le32 (raw + hs - 8) != h || rd_le32 (raw + hs - 4) != 32)
				continue;
			char name[PATH_MAX];
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot)
				*dot = 0;
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT NFF:%s (%ux%u) -> PNG\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h);
			err = ERR_OK;
			if (!testmode)
				err = kyl_write_rgba_png (arg, basedir, name, (uint)-1, w, h, raw + hs);
			goto done;
		}
done:
	FREE (raw);
	return err;
}
