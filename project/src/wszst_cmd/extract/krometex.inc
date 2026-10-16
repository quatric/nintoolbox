// Krome Studios ".tex" (Star Wars: The Force Unleashed, Wii): big-endian {u32 format (0x4c CMPR, 0x4b IA8, 0 RGBA8),
// u32 width, u32 height, u32 data size, u32 0, u32 (mip levels - 1) << 16 | .., u32, u32}, GX pixel data with the whole
// mip chain from 0x20 (file size = 0x20 + data size). The base level is written as PNG.
static enumError extract_krometex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".tex"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 fmt = sz >= 0x20 ? rd_be32 (raw) : ~0u, w = sz >= 0x20 ? rd_be32 (raw + 4) : 0,
		  h = sz >= 0x20 ? rd_be32 (raw + 8) : 0, dsz = sz >= 0x20 ? rd_be32 (raw + 12) : 0;
	const int gx = fmt == 0x4c ? 14 : fmt == 0x4b ? 3 : fmt == 0 ? 6 : -1;
	if (gx < 0 || !w || !h || w > 4096 || h > 4096 || (w & (w - 1)) || (h & (h - 1)) || dsz + 0x20 != sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u8 *rgba = 0;
	const enumError derr = DecodeGXTexture_RGBA (&rgba, w, h, gx, raw + 0x20, dsz, 0, 0, 0);
	FREE (raw);
	if (derr || !rgba)
		return ERR_NOTHING_TO_DO;
	char dest[PATH_MAX];
	beside_source_image_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT KROME-TEX:%s (%ux%u) -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			arg, w, h, dest);
	if (testmode)
	{
		FREE (rgba);
		return ERR_OK;
	}
	return SaveDecodedRGBAToPNG (rgba, w, h, &be_func, dest, 0, opt_overwrite);
}
