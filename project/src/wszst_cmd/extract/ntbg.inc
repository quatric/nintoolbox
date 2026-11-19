// Game Freak Nitro background ("NTBG" / ".5bg", Looksley's Line Up): LE {"NTBG", u16 0xfeff, u16 0x100, u32 size,
// u16 0x10, u16 chunk count} followed by {tag, u32 size incl. 8-byte head} chunks. PALT = {u32 n, n x BGR555};
// BGDT = {u8 depth (0 = 4 bpp with 16-colour sub-palettes, 1 = 8 bpp), u8 0xff, u16 2, u32 screen bytes, u16 w, u16 h
// (tiles), u16 w, u16 h, u32 tile bytes}, then a 32-wide NDS text-BG screen map (tile 10 bits, h/v flip, 4-bit
// palette) and the 8x8 tiles. DFPL (default palette?) is ignored. Decode mode (xx) writes "<file>.png".
static enumError extract_ntbg_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *rgba = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const u8 *pal = 0, *bg = 0;
	uint pal_n = 0, bg_sz = 0;
	if (sz < 0x30 || memcmp (raw, "NTBG", 4) || raw[4] != 0xff || raw[5] != 0xfe || le32 (raw + 8) != sz)
		goto done;
	for (size_t p = raw[0x0c] | raw[0x0d] << 8; p + 8 <= sz;)
	{
		const u32 cs = le32 (raw + p + 4);
		if (cs < 8 || p + cs > sz)
			goto done;
		if (!memcmp (raw + p, "PALT", 4) && cs >= 12)
		{
			pal_n = le32 (raw + p + 8);
			if (pal_n > 256 || 12 + 2 * (size_t)pal_n > cs)
				goto done;
			pal = raw + p + 12;
		}
		else if (!memcmp (raw + p, "BGDT", 4))
		{
			bg = raw + p + 8;
			bg_sz = cs - 8;
		}
		p += cs;
	}
	if (!pal || !bg || bg_sz < 20 || bg[0] > 1)
		goto done;
	{
		const bool bpp8 = bg[0] == 1;
		const uint scr = le32 (bg + 4), tw = bg[8] | bg[9] << 8, th = bg[10] | bg[11] << 8, tsz = le32 (bg + 16);
		const uint ts = bpp8 ? 64 : 32;
		if (!tw || !th || tw > 32 || th > 32 || scr < 2 * 32 * th || (u64)20 + scr + tsz > bg_sz)
			goto done;
		const u8 *map = bg + 20, *tiles = bg + 20 + scr;
		const uint W = tw * 8, H = th * 8;
		rgba = CALLOC (1, (size_t)W * H * 4);
		if (!rgba)
		{
			err = ERR_OUT_OF_MEMORY;
			goto done;
		}
		for (uint ty = 0; ty < th; ty++)
			for (uint tx = 0; tx < tw; tx++)
			{
				const uint e = map[(ty * 32 + tx) * 2] | map[(ty * 32 + tx) * 2 + 1] << 8;
				const uint ti = e & 0x3ff, hf = e >> 10 & 1, vf = e >> 11 & 1, pb = e >> 12;
				if ((ti + 1) * ts > tsz)
					continue;
				const u8 *t = tiles + ti * ts;
				for (uint y = 0; y < 8; y++)
					for (uint x = 0; x < 8; x++)
					{
						const uint sx = hf ? 7 - x : x, sy = vf ? 7 - y : y;
						uint c;
						if (bpp8)
							c = t[sy * 8 + sx];
						else
							c = t[sy * 4 + sx / 2] >> (sx & 1 ? 4 : 0) & 15;
						if (!c)
							continue;
						const uint idx = c + (bpp8 ? 0 : pb * 16);
						const uint col = idx < pal_n ? pal[2 * idx] | pal[2 * idx + 1] << 8 : 0;
						u8 *o = rgba + ((size_t)(ty * 8 + y) * W + tx * 8 + x) * 4;
						o[0] = (col & 31) * 255 / 31;
						o[1] = (col >> 5 & 31) * 255 / 31;
						o[2] = (col >> 10 & 31) * 255 / 31;
						o[3] = 255;
					}
			}
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.png", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE NTBG:%s %ux%u -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, W, H,
				png);
		if (testmode)
			err = ERR_OK;
		else
		{
			err = SaveDecodedRGBAToPNG (rgba, W, H, &be_func, png, 0, true);
			rgba = 0; // owned by SaveDecodedRGBAToPNG
		}
	}
done:
	FREE (rgba);
	FREE (raw);
	return err;
}
