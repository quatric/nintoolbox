// Dokapon Kingdom ".PIM" textures (Sting, Wii; PS2-derived): little-endian header of eight u16 {width, height,
// bits per pixel (4, 8 or 32), palette entry count, palette offset (16), 0, pixel data offset, 0}. Indexed images
// carry an RGBA palette of 4-byte entries whose alpha is the PS2 range 0..0x80, followed by linear indices (4-bit
// ones low nibble first). 32-bit images are plain RGBA with the same alpha range. Decoded to "<file>.png".
static enumError extract_pim_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".pim") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 16)
		goto done;
	const uint w = rd_le16 (raw), h = rd_le16 (raw + 2), bpp = rd_le16 (raw + 4), pn = rd_le16 (raw + 6);
	const size_t po = rd_le16 (raw + 8), off = rd_le16 (raw + 12);
	const size_t need = bpp == 32 ? (size_t)w * h * 4 : (size_t)w * h * bpp / 8;
	if (!w || !h || w > 4096 || h > 4096 || (bpp != 4 && bpp != 8 && bpp != 32) || rd_le16 (raw + 10) || rd_le16 (raw + 14)
		|| (bpp == 4 && (w & 1)) || (bpp != 32 && (!pn || pn > (1u << bpp) || po + 4 * (size_t)pn > off))
		|| (bpp == 32 && pn) || off < 16 || off > sz || need > sz - off)
		goto done;
	u8 *rgba = MALLOC ((size_t)w * h * 4);
	if (!rgba)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	for (size_t i = 0; i < (size_t)w * h; i++)
	{
		u8 *o = rgba + 4 * i;
		const u8 *p;
		if (bpp == 32)
			p = raw + off + 4 * i;
		else
		{
			const uint ix = bpp == 8 ? raw[off + i] : (raw[off + i / 2] >> (4 * (i & 1))) & 15;
			p = ix < pn ? raw + po + 4 * ix : 0;
		}
		if (p)
		{
			o[0] = p[0], o[1] = p[1], o[2] = p[2];
			o[3] = p[3] > 0x7f ? 0xff : p[3] * 2;
		}
		else
			memset (o, 0, 4);
	}
	char png[PATH_MAX];
	snprintf (png, sizeof (png), "%s.png", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE PIM:%s %ux%u %ubpp -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			arg, w, h, bpp, png);
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
