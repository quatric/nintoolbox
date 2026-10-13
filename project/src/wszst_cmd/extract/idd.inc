// PlatinumGames "IDD\0" / "EFF\0" section containers (MadWorld, Wii; ".idd" / ".eff"). Header: magic,
// u32 count, then {u32 id, u32 offset} pairs with ids 0, 1, 2 ...; every section starts with a 4 byte tag
// ("TEX", "EST", "MOD", "TUV", ...), a u32 count and a u32 body offset (0x10). A "TEX" section body (B)
// is {u32 id, u32 n, u32 table = 0xc}, n pairs {u32 texture record, u32 palette header or 0} at B+table.
// Texture record (0x24 bytes): u16 width, u16 height, u32 GX format, u32 pixel data offset (from B),
// then mip / wrap words. Palette header: u32 entries << 16, u32 TLUT format (0 IA8, 1 RGB565, 2 RGB5A3),
// u32 palette offset (from B). Every texture is written as "<file>.<NN>.png" in decode mode (xx).
static enumError extract_idd_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if ((!is_ext (arg, ".idd") && !is_ext (arg, ".eff")) || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x30 || (memcmp (raw, "IDD", 4) && memcmp (raw, "EFF", 4)))
		goto done;
	uint nsec = 0;
	size_t tex = 0;
	for (; nsec < 32 && 8 + 8 * (size_t)nsec + 8 <= sz; nsec++)
	{
		const size_t off = rd_be32 (raw + 12 + 8 * nsec);
		if (rd_be32 (raw + 8 + 8 * nsec) != nsec || off < 0x10 || off + 0x20 > sz)
			break;
		if (!tex && !memcmp (raw + off, "TEX", 4))
			tex = off;
	}
	if (!tex)
		goto done;
	const size_t B = tex + rd_be32 (raw + tex + 12);
	if (B + 0xc > sz)
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
		const uint w = rd_be16 (raw + B + rec), h = rd_be16 (raw + B + rec + 2), fmt = rd_be32 (raw + B + rec + 4);
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
			fprintf (stdlog, "%s%sDECODE IDD-TEX:%s %ux%u fmt %u -> PNG:%s\n", verbose > 0 ? "\n" : "",
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
