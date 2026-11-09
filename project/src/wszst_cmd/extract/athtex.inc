// International Athletics ".TEX" texture (PAK\3 member): BE header {u32 0x04020000 / 0x04027400, u32 format,
// u32 data size, u32 width, u32 height, u32 1}, data at +0x18 and the file ends exactly after it.
// Formats: 9 = C4 and 10 = C8 (16-bit RGB5A3 palette first, then the GX-tiled indices), 11 = RGB5A3 and
// 13 = CMPR. Written beside the source as PNG (rows are stored bottom-up).
static enumError extract_athtex_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".tex"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x20 || sz > 0x4000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint fmt = rd_be32 (raw + 4), dsz = rd_be32 (raw + 8), w = rd_be32 (raw + 12), h = rd_be32 (raw + 16);
	if ((rd_be32 (raw) >> 16) != 0x0402 || rd_be32 (raw + 20) != 1 || (fmt < 9 || fmt > 13 || fmt == 12) || !w || !h || w > 4096
		|| h > 4096 || dsz != sz - 0x18)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".png");
	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT ATH-TEX:%s -> PNG:%s\n", arg, dest);
		return ERR_OK;
	}
	const u8 *p = raw + 0x18;
	u8 *rgba = 0;
	enumError err;
	if (fmt == 9 || fmt == 10)
	{
		const uint pal = fmt == 9 ? 16 : 256;
		if (dsz < pal * 2)
			err = ERR_NOTHING_TO_DO;
		else
			err = DecodeGXTexture_RGBA (&rgba, w, h, fmt == 9 ? 8 : 9, p + pal * 2, dsz - pal * 2, p, pal, 2);
	}
	else
		err = DecodeGXTexture_RGBA (&rgba, w, h, fmt == 11 ? 5 : 14, p, dsz, 0, 0, 0);
	FREE (raw);
	if (err || !rgba)
	{
		FREE (rgba);
		return ERR_NOTHING_TO_DO;
	}
	// rows are stored bottom-up
	for (uint y = 0; y < h / 2; y++)
	{
		u8 *r0 = rgba + (size_t)y * w * 4, *r1 = rgba + (size_t)(h - 1 - y) * w * 4;
		for (size_t k = 0; k < (size_t)w * 4; k++)
		{
			const u8 t = r0[k];
			r0[k] = r1[k];
			r1[k] = t;
		}
	}
	err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, dest, 0, true);
	if (!err && verbose >= 0)
		fprintf (stdlog, "%sEXTRACT ATH-TEX:%s -> PNG:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
