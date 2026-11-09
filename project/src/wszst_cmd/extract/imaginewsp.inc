// Ubisoft Imagine engine ".wsp" sprite (Babysitting Party, Babyz, Beastly; the file inside the LZSS wrapper, see
// imaginelz.inc): little-endian {u32 GX format (4 RGB565, 5 RGB5A3, 6 RGBA8), u32 width, u32 height}, 0xcc padding up
// to 0x20, then the GX tiles for the width rounded up to 4 and the height rounded up to 4 (853x480 is stored as
// 856x480). Written beside the source as PNG.
static enumError extract_imaginewsp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".wsp"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x24 || sz > 0x4000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint fmt = rd_le32 (raw), w = rd_le32 (raw + 4), h = rd_le32 (raw + 8);
	bool pad = true;
	for (uint i = 12; i < 0x20; i++)
		pad = pad && raw[i] == 0xcc;
	const uint pw = (w + 3) & ~3u, ph = (h + 3) & ~3u;
	if (!pad || fmt < 4 || fmt > 6 || !w || !h || w > 4096 || h > 4096
		|| (size_t)pw * ph * (fmt == 6 ? 4 : 2) > sz - 0x20)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".png");
	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT IMAGINE-WSP:%s -> PNG:%s\n", arg, dest);
		return ERR_OK;
	}
	u8 *rgba = 0;
	enumError err = DecodeGXTexture_RGBA (&rgba, pw, ph, fmt, raw + 0x20, (uint)(sz - 0x20), 0, 0, 0);
	FREE (raw);
	if (err || !rgba)
	{
		FREE (rgba);
		return ERR_NOTHING_TO_DO;
	}
	if (pw != w)
		for (uint y = 1; y < h; y++)
			memmove (rgba + (size_t)y * w * 4, rgba + (size_t)y * pw * 4, (size_t)w * 4);
	err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, dest, 0, true);
	if (!err && verbose >= 0)
		fprintf (stdlog, "%sEXTRACT IMAGINE-WSP:%s -> PNG:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
