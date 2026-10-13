// Cursed Mountain (Wii) ".timg" textures, big-endian: bytes ae fe 00 03, u8 1, u8 mip level count, u16 0, u16
// width, u16 0, u16 height, u16 0, then u16 base level size / 32 (@0x10), u8 format selector @0x12 (6 = GX RGB5A3,
// 7 = RGBA8, 8 = CMPR, 0xb = I8, 0xc = IA8), and 0x20 header bytes per mip level (the descriptors are not parsed, the base level starts
// at 0x20 * level count and the smaller levels follow it). Only the
// base level is written, as "<file>.png".
static enumError extract_timg_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".timg") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || memcmp (raw, "\xae\xfe\x00\x03", 4) || raw[5] == 0 || raw[5] > 16)
		goto done;
	const uint w = rd_be16 (raw + 8), h = rd_be16 (raw + 12), sel = raw[0x12];
	const size_t off = 0x20 * (size_t)raw[5];
	const uint gx = sel == 8 ? 14 : sel == 6 ? 5 : sel == 7 ? 6 : sel == 0xb ? 1 : sel == 0xc ? 3 : 0;
	if (!gx || !w || !h || w > 4096 || h > 4096 || off >= sz)
		goto done;
	u8 *rgba = eashpg_decode (gx, raw + off, sz - off, w, h, 0, 0, 0);
	if (!rgba)
		goto done;
	char png[PATH_MAX];
	snprintf (png, sizeof (png), "%s.png", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE TIMG:%s %ux%u fmt %u -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			arg, w, h, gx, png);
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
