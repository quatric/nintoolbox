// Grasshopper Manufacture "GCT0" texture (No More Heroes 1/2 ".BIN"): a 0x40
// header {"GCT0", u32 gx_format, u16 width, u16 height, ...} followed by
// plain GX pixel data (mip levels, when present, are ignored). Decoded to
// "<name>.png" in decode mode (xx) via a synthetic TPL.
static enumError extract_gct0_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size < 0x40 || memcmp (raw, "GCT0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint tpl_fmt = raw[4] << 24 | raw[5] << 16 | raw[6] << 8 | raw[7];
	const uint w = raw[8] << 8 | raw[9], h = raw[10] << 8 | raw[11];
	uint bw = 0, bh = 0, bits = 0;
	switch (tpl_fmt)
	{
		case 0: bw = 8, bh = 8, bits = 4; break; // I4
		case 1: bw = 8, bh = 4, bits = 8; break; // I8
		case 2: bw = 8, bh = 4, bits = 8; break; // IA4
		case 3: bw = 4, bh = 4, bits = 16; break; // IA8
		case 4: bw = 4, bh = 4, bits = 16; break; // RGB565
		case 5: bw = 4, bh = 4, bits = 16; break; // RGB5A3
		case 6: bw = 4, bh = 4, bits = 32; break; // RGBA8
		case 14: bw = 8, bh = 8, bits = 4; break; // CMPR
	}
	const size_t need = bw && w && h ? (size_t)((w + bw - 1) / bw * bw) * ((h + bh - 1) / bh * bh) * bits / 8 : 0;
	if (!need || w > 4096 || h > 4096 || raw_size - 0x40 < need)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char tmp[PATH_MAX], png[PATH_MAX], tmp_png[PATH_MAX];
	snprintf (tmp, sizeof (tmp), "%s.gct0-tmp.tpl", arg);
	snprintf (tmp_png, sizeof (tmp_png), "%s.png", tmp);
	snprintf (png, sizeof (png), "%s.png", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE GCT0:%s -> PNG:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, png);
	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}

	const uint data_off = 0x40;
	u8 *tpl = CALLOC (1, data_off + need);
	if (!tpl)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	u8 *p = tpl;
#define PUT32(v) do { const uint v_ = (v); *p++ = v_ >> 24; *p++ = v_ >> 16; *p++ = v_ >> 8; *p++ = v_; } while (0)
	PUT32 (0x0020af30); PUT32 (1); PUT32 (0x0c);
	PUT32 (0x14); PUT32 (0);
	*p++ = h >> 8; *p++ = h; *p++ = w >> 8; *p++ = w;
	PUT32 (tpl_fmt); PUT32 (data_off); PUT32 (0); PUT32 (0); PUT32 (1); PUT32 (1); PUT32 (0);
#undef PUT32
	memcpy (tpl + data_off, raw + 0x40, need);
	FREE (raw);

	enumError err = SaveFile (tmp, 0, 0, tpl, data_off + need, 0);
	FREE (tpl);
	if (!err)
		err = decode_image_if_possible (tmp);
	if (!err && rename (tmp_png, png))
		err = ERROR1 (ERR_CANT_CREATE, "Can't create PNG: %s\n", png);
	unlink (tmp);
	return err;
}
