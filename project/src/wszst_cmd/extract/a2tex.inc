// K11 - Kommissare im Einsatz (Germany) textures ".a2" (wii/Characters/Textures/*.a2), big-endian: {u32 GX format (0 I4, 1 I8, 2 IA4, 3 IA8, 4 RGB565, 5 RGB5A3, 6 RGBA8, 14 CMPR), u32 width, u32 height,
// u32 mip count, u32 0, u32 0, u32 size of the first level, first level in GX tile layout from 0x1c, further levels behind it}. Only the first level is decoded, to "<file>.png".
static enumError extract_a2tex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".a2"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x30)
		goto done;
	{
		const u32 fmt = rd_be32 (raw), w = rd_be32 (raw + 4), h = rd_be32 (raw + 8), mips = rd_be32 (raw + 12), ds = rd_be32 (raw + 0x18);
		if (fmt > 14 || (fmt > 6 && fmt != 14) || !w || !h || w > 4096 || h > 4096 || !mips || mips > 16 || rd_be32 (raw + 16) || rd_be32 (raw + 20))
			goto done;
		const uint bw = fmt == 0 || fmt == 14 ? 8 : fmt == 1 || fmt == 2 ? 8 : 4, bh = fmt == 0 || fmt == 14 ? 8 : fmt == 3 || fmt >= 4 ? 4 : 4;
		const uint bpp = fmt == 0 || fmt == 14 ? 4 : fmt == 1 || fmt == 2 ? 8 : fmt == 6 ? 32 : 16;
		const u64 aw = (w + bw - 1) / bw * bw, ah = (h + bh - 1) / bh * bh;
		if (ds != aw * ah * bpp / 8 || 0x1c + (u64)ds > sz)
			goto done;
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.png", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE A2:%s (%ux%u fmt %u) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, fmt, png);
		err = testmode ? ERR_OK : wiitex_write_raw (png, w, h, fmt, raw + 0x1c, ds);
	}
done:
	FREE (raw);
	return err;
}
