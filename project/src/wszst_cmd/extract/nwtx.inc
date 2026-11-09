// Coresoft "NWTX" texture (".wtx", Fantasy Aquarium World jpack members): big-endian header
// {"NWTX", u32 type (1 = CMPR, 2 = I8, 3 = RGB5A3), u32 width, u32 height, u32 mip count, u32 base level size,
// 0, 0}, pixel data at +0x20 (base level first, then the mip chain). The base level is written as PNG.
static enumError extract_nwtx_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".wtx"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x28 || sz > 0x7fffffff || memcmp (raw, "NWTX", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint type = rd_be32 (raw + 4), w = rd_be32 (raw + 8), h = rd_be32 (raw + 12), base = rd_be32 (raw + 20);
	static const uint gx[4] = {0, 14, 1, 5};
	if (type < 1 || type > 3 || !w || !h || w > 4096 || h > 4096 || base > sz - 0x20)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".png");
	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT NWTX:%s -> PNG:%s\n", arg, dest);
		return ERR_OK;
	}
	u8 *rgba = 0;
	enumError err = DecodeGXTexture_RGBA (&rgba, w, h, gx[type], raw + 0x20, base, 0, 0, 0);
	FREE (raw);
	if (err || !rgba)
	{
		FREE (rgba);
		return ERR_NOTHING_TO_DO;
	}
	err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, dest, 0, true);
	if (!err && verbose >= 0)
		fprintf (stdlog, "%sEXTRACT NWTX:%s -> PNG:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
