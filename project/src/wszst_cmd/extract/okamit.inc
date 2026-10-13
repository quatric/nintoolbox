// Clover / Okami (Wii) ".t" texture files: the body of an IDD "TEX" section on its own. {u32 id, u32 count,
// u32 table = 0xc}, count pairs {texture record, palette header or 0}; texture record = u16 height, u16 width (swapped against IDD),
// u32 GX format, u32 pixel data offset, palette header = u32 entries << 16, u32 TLUT format, u32 offset (all
// offsets from the start of the file). Every texture is written as "<file>.NN.png" in decode mode (xx).
static enumError extract_okamit_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".t") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t B = 0;
	if (sz < 0x40 || rd_be32 (raw + 8) != 0xc)
		goto done;
	const uint n = rd_be32 (raw + B + 4), tab = rd_be32 (raw + B + 8);
	if (!n || n > 512 || B + tab + 8 * (size_t)n > sz)
		goto done;
	err = ERR_OK;
	uint done_n = 0;
	for (uint i = 0; i < n && !err; i++)
	{
		const size_t rec = rd_be32 (raw + B + tab + 8 * i), ph = rd_be32 (raw + B + tab + 8 * i + 4);
		if (B + rec + 0x10 > sz)
			continue;
		const uint h = rd_be16 (raw + B + rec), w = rd_be16 (raw + B + rec + 2), fmt = rd_be32 (raw + B + rec + 4);
		const size_t dat = B + rd_be32 (raw + B + rec + 8);
		if (!w || !h || w > 2048 || h > 2048 || dat >= sz)
			continue;
		const u8 *pal = 0;
		uint pn = 0, pfmt = 0;
		if (ph && B + ph + 12 <= sz)
		{
			pn = rd_be32 (raw + B + ph) >> 16;
			pfmt = rd_be32 (raw + B + ph + 4);
			const size_t po = B + rd_be32 (raw + B + ph + 8);
			if (pn && po + 2 * (size_t)pn <= sz)
				pal = raw + po;
			else
				pn = 0;
		}
		u8 *rgba = eashpg_decode (fmt, raw + dat, sz - dat, w, h, pal, pn, pfmt);
		if (!rgba)
			continue;
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.%02u.png", arg, i);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE OKAMI-TEX:%s %ux%u fmt %u -> PNG:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, w, h, fmt, png);
		done_n++;
		if (testmode)
			FREE (rgba);
		else
			err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true);
	}
	if (!err && !done_n)
		err = ERR_NOTHING_TO_DO;
done:
	FREE (raw);
	return err;
}
