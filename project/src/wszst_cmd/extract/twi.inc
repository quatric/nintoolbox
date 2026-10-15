// Exient "TWI" textures (".twi"; Need for Speed: Hot Pursuit, Wii), big-endian: "TWI\0", u32 width @4, u32 height @8,
// u32 mip level count @0xc, u32 format @0x10 (0 = IA8, 3 = RGBA8, 4 = C4, 5 = C8, 251 = CMPR), 0xfa @0x14 for the paletted formats, palette
// byte size @0x18, u32 data size (all mip levels, excluding the palette) @0x1c, then, for the paletted formats, the
// RGB5A3 palette at 0x20 and the GX tiled data (largest level first). The base level is
// written as "<file>.png".
static enumError extract_twi_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".twi") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || memcmp (raw, "TWI\0", 4))
		goto done;
	const uint w = rd_be32 (raw + 4), h = rd_be32 (raw + 8), fmt = rd_be32 (raw + 0x10);
	const size_t dsz = rd_be32 (raw + 0x1c);
	const uint gx = fmt == 0 ? 3 : fmt == 3 ? 6 : fmt == 4 ? 8 : fmt == 5 ? 9 : fmt == 251 ? 14 : 0;
	if (!gx || !w || !h || w > 4096 || h > 4096 || dsz > sz - 0x20)
		goto done;
	const uint pn = gx == 8 || gx == 9 ? rd_be32 (raw + 0x18) / 2 : 0;
	if ((gx == 8 || gx == 9) && (pn != (gx == 8 ? 16u : 256u) || 0x20 + 2 * (size_t)pn + dsz > sz))
		goto done;
	u8 *rgba = eashpg_decode (gx, raw + 0x20 + 2 * pn, dsz, w, h, pn ? raw + 0x20 : 0, pn, 2);
	if (!rgba)
		goto done;
	char png[PATH_MAX];
	snprintf (png, sizeof (png), "%s.png", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE TWI:%s %ux%u fmt %u -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
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
