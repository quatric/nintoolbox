// Super Fruit Fall (Wii) ".tdw" texture sets, big-endian: u32 texture count @0, then per texture a 0x40 byte
// zero padded name, u32 GX texture format (4 = RGB565, 5 = RGB5A3, 6 = RGBA8, 14 = CMPR...), u32 3, u32 0, u32 width,
// u32 height, u32 data size, the GX tiled data and 4 padding bytes. Every texture is written as "<file>.<name>.png".
static enumError extract_tdw_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".tdw") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 4 + 0x58)
		goto done;
	const uint count = rd_be32 (raw);
	if (!count || count > 256)
		goto done;

	// Validate the whole chain before writing anything.
	size_t pos = 4;
	for (uint i = 0; i < count; i++)
	{
		if (pos + 0x58 > sz)
			goto done;
		const uint fmt = rd_be32 (raw + pos + 0x40), w = rd_be32 (raw + pos + 0x4c), h = rd_be32 (raw + pos + 0x50);
		const size_t dsz = rd_be32 (raw + pos + 0x54);
		if (rd_be32 (raw + pos + 0x44) != 3 || rd_be32 (raw + pos + 0x48) != 0 || !w || !h || w > 4096 || h > 4096
			|| (fmt != 1 && fmt != 3 && fmt != 4 && fmt != 5 && fmt != 6 && fmt != 14) || dsz + 4 > sz - pos - 0x58)
			goto done;
		pos += 0x58 + dsz + 4;
	}

	err = ERR_OK;
	pos = 4;
	for (uint i = 0; i < count && err == ERR_OK; i++)
	{
		const uint fmt = rd_be32 (raw + pos + 0x40), w = rd_be32 (raw + pos + 0x4c), h = rd_be32 (raw + pos + 0x50);
		const size_t dsz = rd_be32 (raw + pos + 0x54);
		char name[0x41];
		memcpy (name, raw + pos, 0x40);
		name[0x40] = 0;
		for (char *p = name; *p; p++)
			if (*p == '/' || *p == '\\' || (u8)*p < 0x20)
				*p = '_';
		if (!*name)
			snprintf (name, sizeof (name), "%03u", i);
		u8 *rgba = eashpg_decode (fmt, raw + pos + 0x58, dsz, w, h, 0, 0, 0);
		pos += 0x58 + dsz + 4;
		if (!rgba)
			continue;
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.%s.png", arg, name);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE TDW:%s %ux%u fmt %u -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
				arg, w, h, fmt, png);
		if (testmode)
			FREE (rgba);
		else
			err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true);
	}
done:
	FREE (raw);
	return err;
}
