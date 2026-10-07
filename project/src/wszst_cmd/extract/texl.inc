// Petz engine "TEXL" texture (Cranium Kabookii and relatives): a 0x19-byte
// header {"TEXL", u32 version, u32 0, u32 fmt, u16 w, u16 h, ...} followed by
// raw GX pixel data and an 8-byte trailer. Decoded to "<name>.png" in decode
// mode (xx) by wrapping the pixels in a synthetic TPL. Supported: CMPR
// (4 bpp), RGBA8 (32 bpp) and the colour plane of CMPR+alpha (8 bpp) files.
static enumError extract_texl_file (ccp arg, ccp basedir, uint depth)
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
	if (raw_size < 0x40 || memcmp (raw, "TEXL", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint w = raw[0x10] << 8 | raw[0x11], h = raw[0x12] << 8 | raw[0x13];
	const size_t body = raw_size - 0x19 - 8;
	uint tpl_fmt = 0;
	size_t need = 0;
	if (w && h && !(w & 7) && !(h & 7))
	{
		if (body == (size_t)w * h / 2 || body == (size_t)w * h)
			tpl_fmt = 0x0e, need = (size_t)w * h / 2;
		else if (body == (size_t)w * h * 4)
			tpl_fmt = 0x06, need = body;
	}
	if (!tpl_fmt)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char tmp[PATH_MAX], png[PATH_MAX], tmp_png[PATH_MAX];
	snprintf (tmp, sizeof (tmp), "%s.texl-tmp.tpl", arg);
	snprintf (tmp_png, sizeof (tmp_png), "%s.png", tmp);
	snprintf (png, sizeof (png), "%s.png", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE TEXL:%s -> PNG:%s\n", verbose > 0 ? "\n" : "",
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
	memcpy (tpl + data_off, raw + 0x19, need);
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
