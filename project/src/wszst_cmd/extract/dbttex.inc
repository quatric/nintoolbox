// Dragon Ball Z Budokai Tenkaichi 2 (Wii) "*.dbt" texture bundles, big-endian: {u32 3, u32 8, ...}, three GX texture descriptors at 0x100 (+0x24 stride: {0, u16 width, u16 height, u32 gx format, ...}) and
// the texture data from 0x180, each 512x512 CMPR image back to back. Each is written to "<file>.N.png".
static enumError extract_dbttex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".dbt"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x180 || rd_be32 (raw) != 3 || rd_be32 (raw + 4) != 8)
		goto done;
	const uint w = rd_be16 (raw + 0x104), h = rd_be16 (raw + 0x106), fmt = rd_be32 (raw + 0x108);
	if (fmt != 14 || w < 8 || h < 8 || w > 2048 || h > 2048)
		goto done;
	const size_t one = (size_t)((w + 7) / 8) * ((h + 7) / 8) * 32;
	err = ERR_OK;
	for (uint i = 0; 0x180 + (i + 1) * one <= sz && !err; i++)
	{
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.%u.png", arg, i);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE DBT:%s (%ux%u CMPR #%u) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, i, png);
		if (!testmode)
			err = wiitex_write_raw (png, w, h, 14, raw + 0x180 + i * one, one);
	}
done:
	FREE (raw);
	return err;
}
