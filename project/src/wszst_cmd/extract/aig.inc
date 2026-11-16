// "ALIG" GX textures (Shonen Jump's One Piece: Unlimited Adventure, Wii: "material/*.aig"): a 0x40 byte header {"ALIG", u16 LE 0x0102, 2 pad bytes,
// char format[4] ("GIA4" = IA4, "G5A3" = RGB5A3, "G565" = RGB565, "GC32" = RGBA8, "GCCP" / "GACC" = CMPR), u32 0, u16 LE width, u16 LE height, u32 LE 1, u32 LE 0x10,
// u32 LE 0x40, u32 BE level-0 data size, ...} followed by the GX tiled pixel data (mip levels follow level 0). Level 0 is written as "<name>.png".
static enumError extract_aig_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".aig"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *rgba = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 0x40 && !memcmp (raw, "ALIG", 4) && raw[8] == 'G')
	{
		static const struct { const char *tag; uint fmt; } fm[] = { { "GIA4", 2 }, { "G5A3", 5 }, { "G565", 4 }, { "GC32", 6 }, { "GCCP", 14 }, { "GACC", 14 } };
		uint fmt = 99;
		for (uint i = 0; i < sizeof (fm) / sizeof (fm[0]); i++)
			if (!memcmp (raw + 8, fm[i].tag, 4))
				fmt = fm[i].fmt;
		const uint w = rd_le16 (raw + 0x10), h = rd_le16 (raw + 0x12), ds = rd_be32 (raw + 0x20);
		if (fmt != 99 && w && h && w <= 4096 && h <= 4096 && ds && 0x40 + (u64)ds <= sz
			&& !DecodeGXTexture_RGBA (&rgba, w, h, fmt, raw + 0x40, ds, 0, 0, 0) && rgba)
		{
			char name[PATH_MAX];
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot)
				*dot = 0;
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE AIG:%s (%ux%u %.4s) -> PNG\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, (ccp)raw + 8);
			err = testmode ? ERR_OK : kyl_write_rgba_png (arg, basedir, name, (uint)-1, w, h, rgba);
		}
	}
	FREE (rgba);
	FREE (raw);
	return err;
}
