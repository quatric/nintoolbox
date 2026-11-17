// Nintendo DS-style bitmap fonts "NFTR" ("RTFN" magic, little-endian; Hannah Montana: The Movie, Wii): the "CGLP" (stored "PLGC") block holds the glyph tiles {u32 size, u8 tile width, u8 tile height, u16 tile bytes,
// s8 underline, u8 max width, u8 bpp (1/2/4/8), u8 rotation} followed by tile bitmaps (bits MSB first, row major, bpp bits per pixel). All glyph tiles are laid out in a 32-column atlas (tile index order, coverage as
// black with alpha) and written to "<file>.png". Character mapping (CMAP) and width (CWDH) blocks are not applied.
static enumError extract_nftr_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	u8 *raw = 0, *rgba = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || memcmp (raw, "RTFN", 4) || rd_le16 (raw + 4) != 0xfeff)
		goto done;
	{
		const uint hdr = rd_le16 (raw + 12), nblk = rd_le16 (raw + 14);
		size_t o = hdr;
		const u8 *cg = 0;
		for (uint b = 0; b < nblk && o + 8 <= sz; b++)
		{
			const u32 bs = rd_le32 (raw + o + 4);
			if (!memcmp (raw + o, "PLGC", 4))
				cg = raw + o;
			if (bs < 8 || o + bs > sz)
				break;
			o += bs;
		}
		if (!cg)
			goto done;
		const u32 cs = rd_le32 (cg + 4);
		const uint tw = cg[8], th = cg[9], tb = rd_le16 (cg + 10), bpp = cg[14];
		if (!tw || !th || tw > 64 || th > 64 || !tb || (bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8) || (size_t)(cg - raw) + cs > sz || cs < 16 + tb || tb * 8 < tw * th * bpp)
			goto done;
		const uint n = (cs - 16) / tb, cols = n < 32 ? n : 32, rows = (n + cols - 1) / cols, W = cols * tw, H = rows * th;
		if (!n || W > 8192 || H > 16384)
			goto done;
		rgba = CALLOC ((size_t)W * H, 4);
		if (!rgba)
			goto done;
		const uint maxv = (1u << bpp) - 1;
		for (uint t = 0; t < n; t++)
		{
			const u8 *tile = cg + 16 + (size_t)t * tb;
			for (uint y = 0; y < th; y++)
				for (uint x = 0; x < tw; x++)
				{
					const uint bit = (y * tw + x) * bpp;
					uint v = 0;
					for (uint k = 0; k < bpp; k++)
						v = (v << 1) | ((tile[(bit + k) >> 3] >> (7 - ((bit + k) & 7))) & 1);
					u8 *p = rgba + ((size_t)((t / cols) * th + y) * W + (t % cols) * tw + x) * 4;
					p[0] = p[1] = p[2] = 0;
					p[3] = (u8)(v * 255 / maxv);
				}
		}
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.png", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE NFTR:%s (%u glyphs %ux%u, %u bpp) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, tw, th, bpp, png);
		err = ERR_OK;
		if (!testmode)
		{
			SaveDecodedRGBAToPNG (rgba, W, H, &be_func, png, 0, true); // takes ownership of rgba
			rgba = 0;
		}
	}
done:
	FREE (rgba);
	FREE (raw);
	return err;
}
