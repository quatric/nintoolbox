// Ubisoft Imagine engine ".tan" animated 2D texture (Babysitting Party, Babyz, Beastly face/mouth animations;
// inside the LZSS wrapper, see imaginelz.inc): big-endian {u32 frames, u32 width, u32 height, u32 0}, a 0x210-byte
// RGB565 palette (256 used entries), then "frames" GX C8 images (8x4 tiles) of width * height bytes each.
// Written as "<file>.d/frame_NN.png".
static enumError extract_imaginetan_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".tan"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x230 || sz > 0x8000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint nf = rd_be32 (raw), w = rd_be32 (raw + 4), h = rd_be32 (raw + 8);
	if (!nf || nf > 1024 || rd_be32 (raw + 12) || !w || !h || w > 2048 || h > 2048 || (w & 7) || (h & 3)
		|| sz != 0x220 + (size_t)nf * w * h)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT IMAGINE-TAN:%s (%u frames, %ux%u) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nf, w, h, dest);
	enumError err = ERR_OK;
	for (uint i = 0; i < nf && !err && !testmode; i++)
	{
		u8 *rgba = 0;
		if (DecodeGXTexture_RGBA (&rgba, w, h, 9, raw + 0x220 + (size_t)i * w * h, w * h, raw + 16, 256, 1) || !rgba)
		{
			FREE (rgba);
			err = ERR_NOTHING_TO_DO;
			break;
		}
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%sframe_%02u.png", dest, basedir ? basedir : "", i);
		err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true);
	}
	FREE (raw);
	return err;
}
