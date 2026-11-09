// Illvelo / Radirgy Noa (Wii, Sonic Team engine port of Sega's Dreamcast PVR): "XIBG" (= GBIX reversed) {u32 BE 8,
// u64 global index}, "TRVP" (= PVRT reversed) {u32 BE size, u32 BE type, u16 BE width, u16 BE height}, texels at 0x20.
// type = (data kind << 8) | pixel format. Data kind 0x45 = raw 4x4 tiled big-endian 16 bit texels with pixel format
// 0 = ARGB1555, 1 = RGB565, 2 = ARGB4444 (every sample: size == width * height * 2); 0x4e = GX CMPR (4 bit per pixel,
// size == width * height / 2); 0x48 (toon.pvr) is not decoded. Written as "<file>.png" beside the source (or beside the
// .PVR written by the Radirgy archive extractor).
static bool segapvr_png (const u8 *d, size_t n, ccp png_path)
{
	if (n < 0x20 || memcmp (d, "XIBG", 4) || memcmp (d + 0x10, "TRVP", 4))
		return false;
	const uint kind = d[0x1a], pf = d[0x1b], w = rd_be16 (d + 0x1c), h = rd_be16 (d + 0x1e);
	if (!w || !h || w > 4096 || h > 4096 || (w & 7) || (h & 7) || pf > 2 || (kind != 0x45 && kind != 0x4e))
		return false;
	const size_t need = kind == 0x4e ? (size_t)w * h / 2 : (size_t)w * h * 2;
	if (0x20 + need > n)
		return false;
	u8 *rgba = 0;
	if (kind == 0x4e)
	{
		if (DecodeGXTexture_RGBA (&rgba, w, h, 14, d + 0x20, need, 0, 0, 0) || !rgba)
		{
			FREE (rgba);
			return false;
		}
	}
	else
	{
		rgba = MALLOC ((size_t)w * h * 4);
		if (!rgba)
			return false;
		const u8 *s = d + 0x20;
		for (uint ty = 0; ty < h; ty += 4)
			for (uint tx = 0; tx < w; tx += 4)
				for (uint y = 0; y < 4; y++)
					for (uint x = 0; x < 4; x++, s += 2)
					{
						const uint v = (uint)s[0] << 8 | s[1];
						u8 *o = rgba + ((size_t)(ty + y) * w + tx + x) * 4;
						if (pf == 1)
						{
							const uint r = v >> 11, g = (v >> 5) & 63, b = v & 31;
							o[0] = (u8)(r << 3 | r >> 2), o[1] = (u8)(g << 2 | g >> 4), o[2] = (u8)(b << 3 | b >> 2), o[3] = 255;
						}
						else if (pf == 0)
						{
							const uint r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
							o[0] = (u8)(r << 3 | r >> 2), o[1] = (u8)(g << 3 | g >> 2), o[2] = (u8)(b << 3 | b >> 2);
							o[3] = v & 0x8000 ? 255 : 0;
						}
						else
						{
							o[0] = (u8)(((v >> 8) & 15) * 17), o[1] = (u8)(((v >> 4) & 15) * 17), o[2] = (u8)((v & 15) * 17);
							o[3] = (u8)(((v >> 12) & 15) * 17);
						}
					}
	}
	// SaveDecodedRGBAToPNG takes ownership of the pixel buffer
	return !SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png_path, 0, true);
}

static enumError extract_segapvr_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".pvr"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x20 || memcmp (raw, "XIBG", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".png");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SEGA-PVR:%s -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
	const bool ok = testmode || segapvr_png (raw, sz, dest);
	FREE (raw);
	return ok ? ERR_OK : ERR_NOTHING_TO_DO;
}
