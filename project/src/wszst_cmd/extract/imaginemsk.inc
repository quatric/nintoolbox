// Ubisoft Imagine engine ".msk" 8-bit mask (Babysitting Party, Babyz, Beastly; inside the LZSS wrapper, see
// imaginelz.inc): little-endian {u32 width, u32 height}, 16 bytes of 0xcc, then width * height bytes of 8-bit
// values (row-major). Written beside the source as a greyscale PNG.
static enumError extract_imaginemsk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".msk"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x19 || sz > 0x4000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint w = rd_le32 (raw), h = rd_le32 (raw + 4);
	bool pad = true;
	for (uint i = 8; i < 24; i++)
		pad = pad && raw[i] == 0xcc;
	if (!pad || !w || !h || w > 8192 || h > 8192 || (u64)w * h != sz - 24)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".png");
	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT IMAGINE-MSK:%s -> PNG:%s\n", arg, dest);
		return ERR_OK;
	}
	u8 *rgba = MALLOC ((size_t)w * h * 4);
	if (!rgba)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	for (size_t i = 0; i < (size_t)w * h; i++)
	{
		const u8 v = raw[24 + i];
		rgba[4 * i] = rgba[4 * i + 1] = rgba[4 * i + 2] = v;
		rgba[4 * i + 3] = 255;
	}
	FREE (raw);
	const enumError err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, dest, 0, true);
	if (!err && verbose >= 0)
		fprintf (stdlog, "%sEXTRACT IMAGINE-MSK:%s -> PNG:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
