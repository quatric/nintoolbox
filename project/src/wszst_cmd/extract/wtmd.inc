// CRI-style "WTMD" textures (Arc Rise Fantasia, Wii; ".wtm"). Big-endian header: "WTMD", u32 0x20, u16 width,
// u16 height, u8 GX texture format (4 RGB565, 6 RGBA8, 8 CI4, 9 CI8, 14 CMPR), u8 palette format (1 RGB565,
// 2 RGB5A3, 3 IA8; 0 = none), two flag bytes, u32 pixel data offset @0x10 (the palette sits at 0x20, 16 or
// 256 entries), u32 0x20000 / 0x10000. Written to "<file>.png" in decode mode (xx).
static enumError extract_wtmd_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".wtm") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x30 || memcmp (raw, "WTMD", 4) || rd_be32 (raw + 4) != 0x20)
		goto done;
	const uint w = rd_be16 (raw + 8), h = rd_be16 (raw + 10), fmt = raw[12], pf = raw[13];
	const size_t off = rd_be32 (raw + 0x10);
	const uint pn = fmt == 8 ? 16 : fmt == 9 ? 256 : 0;
	if (!w || !h || w > 2048 || h > 2048 || off >= sz || (pn && (!pf || pf > 3 || 0x20 + 2 * (size_t)pn > off)))
		goto done;
	u8 *rgba = eashpg_decode (fmt, raw + off, sz - off, w, h, pn ? raw + 0x20 : 0, pn, pf ? pf - 1 : 0);
	if (!rgba)
		goto done;
	char png[PATH_MAX];
	snprintf (png, sizeof (png), "%s.png", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE WTMD:%s %ux%u fmt %u -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			arg, w, h, fmt, png);
	if (testmode)
	{
		FREE (rgba);
		err = ERR_OK;
	}
	else
		err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true);
done:
	FREE (raw);
	return err;
}
