// Marvelous "FIPAFTEX" texture sets (Harvest Moon: Animal Parade, the "TEX" members of pBin containers). A 0x40
// byte file header ("FIPAFTEX", u32 version, count) is followed by the textures back to back; each is a 0x40-byte
// header {u16 width @0x14, u16 height @0x16, u32 GX format @0x18, u32 mip count @0x24} and the GX pixel data (base
// level followed by the mips). Every texture is written as "<file>.NN.png" in decode mode (xx).
static uint ftex_level_bytes (uint fmt, uint w, uint h)
{
	switch (fmt)
	{
		case 0: case 8: return (w + 7) / 8 * 8 * ((h + 7) / 8 * 8) / 2;
		case 1: case 2: case 9: return (w + 7) / 8 * 8 * ((h + 3) / 4 * 4);
		case 3: case 4: case 5: return (w + 3) / 4 * 4 * ((h + 3) / 4 * 4) * 2;
		case 6: return (w + 3) / 4 * 4 * ((h + 3) / 4 * 4) * 4;
		case 14: return (w + 7) / 8 * 8 * ((h + 7) / 8 * 8) / 2;
	}
	return 0;
}

static enumError extract_ftex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x80 || memcmp (raw, "FIPAFTEX", 8))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_OK;
	uint done_n = 0;
	for (size_t p = 0x40; p + 0x40 < sz && !err && done_n < 256;)
	{
		const uint w = rd_be16 (raw + p + 0x14), h = rd_be16 (raw + p + 0x16), fmt = rd_be32 (raw + p + 0x18);
		uint mips = rd_be32 (raw + p + 0x24);
		if (!mips || mips > 12)
			mips = 1;
		const size_t base = ftex_level_bytes (fmt, w, h);
		if (!w || !h || w > 4096 || h > 4096 || !base || p + 0x40 + base > sz)
			break;
		size_t total = 0;
		for (uint m = 0, lw = w, lh = h; m < mips; m++, lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1)
			total += ftex_level_bytes (fmt, lw, lh);
		u8 *rgba = eashpg_decode (fmt, raw + p + 0x40, sz - p - 0x40, w, h, 0, 0, 0);
		if (rgba)
		{
			char png[PATH_MAX];
			snprintf (png, sizeof (png), "%s.%02u.png", arg, done_n);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE FTEX:%s %ux%u fmt %u -> PNG:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", arg, w, h, fmt, png);
			if (testmode)
				FREE (rgba);
			else
				err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true);
			done_n++;
		}
		p += 0x40 + total;
	}
	FREE (raw);
	return err ? err : done_n ? ERR_OK : ERR_NOTHING_TO_DO;
}
