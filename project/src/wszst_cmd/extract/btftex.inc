// Totsugeki!! Famicom Wars VS (Wii) "*.btf" texture/font bundles: {"FTBG", u32 size, u32 count (little-endian)}, then chunks {"DXTG" (GTXD), u32 chunk size (LE)}. Every chunk holds big-endian {char name[0x20], u32 width,
// u32 height, u32 1, u32 id, char format[8] (reversed text right-aligned: "A8R8G8B8", "I8", "IA4", "P4", "P8"), char palette format[8]}, a "PAL " block (u32 size, palette from +12 as GX RGB5A3) and a "PIM "
// block; the GX-tiled pixel data are the last width * height * bpp bytes of the chunk. Every chunk is decoded to "<file>.NN.png". Files without the FTBG magic (ObjButton_*, some fonts) are not decoded.
static enumError extract_btftex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".btf"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x100 || memcmp (raw, "FTBG", 4))
		goto done;
	const uint count = rd_le32 (raw + 8);
	size_t p = 12;
	for (uint idx = 0; idx < count && idx < 256 && p + 8 <= sz && !(err && err != ERR_NOTHING_TO_DO); idx++)
	{
		const size_t csz = rd_le32 (raw + p + 4), c = p + 8;
		p = c + csz;
		if (csz < 0x80 || p > sz)
			break;
		const u8 *h = raw + c, *end = raw + c + csz;
		const uint w = rd_be32 (h + 0x20), ht = rd_be32 (h + 0x24);
		if (!w || !ht || w > 2048 || ht > 2048)
			continue;
		char fmt[9];
		for (uint i = 0; i < 8; i++)
			fmt[i] = (char)h[0x37 - i];
		fmt[8] = 0;
		const char *f = fmt;
		while (*f == 0)
			f++;
		int gx = -1;
		uint bits = 0;
		if (!strcmp (f, "A8R8G8B8"))
			gx = 6, bits = 32;
		else if (!strcmp (f, "I8"))
			gx = 1, bits = 8;
		else if (!strcmp (f, "IA4"))
			gx = 2, bits = 8;
		else if (!strcmp (f, "IA8"))
			gx = 3, bits = 16;
		else if (!strcmp (f, "RGB565"))
			gx = 4, bits = 16;
		else if (!strcmp (f, "RGB5A3"))
			gx = 5, bits = 16;
		else if (!strcmp (f, "I4"))
			gx = 0, bits = 4;
		else if (!strcmp (f, "P4"))
			gx = 100, bits = 4;
		else if (!strcmp (f, "P8"))
			gx = 101, bits = 8;
		if (gx < 0)
			continue;
		const size_t dsz = (size_t)w * ht * bits / 8;
		if (dsz + 0x70 > csz)
			continue;
		const u8 *data = end - dsz;
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.%02u.png", arg, idx);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE BTF:%s (%ux%u %s) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, ht, f, png);
		if (testmode)
		{
			err = ERR_OK;
			continue;
		}
		if (gx < 100)
		{
			err = wiitex_write_raw (png, w, ht, gx, data, (uint)dsz);
			continue;
		}
		// palette: "PAL " block (tag, u32 size, data); entries are the last colours of the block
		const u8 *pal = 0;
		uint ncol = 0;
		for (const u8 *q = h + 0x40; q + 8 <= end; q++)
			if (!memcmp (q, " LAP", 4) || !memcmp (q, "PAL ", 4))
			{
				const uint ps = rd_le32 (q + 4);
				if (ps >= 2 && q + 8 + ps <= end)
					pal = q + 8, ncol = ps / 2;
				break;
			}
		if (!pal)
			continue;
		const uint bw = gx == 100 ? 8 : 8, bh = gx == 100 ? 8 : 4;
		u8 *rgba = CALLOC (1, (size_t)w * ht * 4);
		if (!rgba)
			break;
		size_t o = 0;
		for (uint ty = 0; ty < ht; ty += bh)
			for (uint tx = 0; tx < w; tx += bw)
				for (uint y = 0; y < bh; y++)
					for (uint x = 0; x < bw; x++, o++)
					{
						const uint ci = gx == 100 ? (data[o >> 1] >> ((o & 1) ? 0 : 4)) & 15 : data[o];
						if (tx + x >= w || ty + y >= ht)
							continue;
						u8 *d = rgba + ((size_t)(ty + y) * w + tx + x) * 4;
						const uint v = ci < ncol ? rd_be16 (pal + 2 * ci) : 0;
						if (v & 0x8000)
							d[0] = (v >> 10 & 31) * 255 / 31, d[1] = (v >> 5 & 31) * 255 / 31, d[2] = (v & 31) * 255 / 31, d[3] = 255;
						else
							d[0] = (v >> 8 & 15) * 17, d[1] = (v >> 4 & 15) * 17, d[2] = (v & 15) * 17, d[3] = (v >> 12 & 7) * 255 / 7;
					}
		SaveDecodedRGBAToPNG (rgba, w, ht, &be_func, png, 0, true); // takes ownership of rgba
		err = ERR_OK;
	}
done:
	FREE (raw);
	return err;
}
