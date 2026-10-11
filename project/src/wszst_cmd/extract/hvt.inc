// Havok-style ".hvt" texture (Aladdin Magic Racer, "texWii/*.hvt"): a 0x18-byte big-endian header
// {" IVH", u32 version (2 or 6), u32 format tag, u32 width, u32 height, u32 bits per pixel} followed by a
// plain Wii GX pixel chain without mips: "S3TW" = CMPR, "ARGB" = RGBA8, "G8A8" = IA8, "GRY8" = I8.
// Decoded to "<name>.hvt.png" in decode mode (xx) by wrapping the pixels in a synthetic TPL.
static enumError extract_hvt_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;
	if (!is_ext (arg, ".hvt"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint w = raw_size >= 0x18 ? rd_be32 (raw + 0x0c) : 0, h = raw_size >= 0x18 ? rd_be32 (raw + 0x10) : 0;
	uint tpl_fmt = 0, bpp = 0;
	if (raw_size >= 0x18)
	{
		const uint ver = rd_be32 (raw + 4), pb = rd_be32 (raw + 0x14);
		if (ver == 2 || ver == 6)
		{
			if (!memcmp (raw + 8, "S3TW", 4) && pb == 4)
				tpl_fmt = 0x0e, bpp = 4;
			else if (!memcmp (raw + 8, "ARGB", 4) && pb == 32)
				tpl_fmt = 0x06, bpp = 32;
			else if (!memcmp (raw + 8, "G8A8", 4) && pb == 16)
				tpl_fmt = 0x03, bpp = 16;
			else if (!memcmp (raw + 8, "GRY8", 4) && pb == 8)
				tpl_fmt = 0x01, bpp = 8;
		}
	}
	if (raw_size < 0x18 || memcmp (raw, " IVH", 4) || !tpl_fmt || !w || !h || (w & 7) || (h & 7) || w > 4096
	    || h > 4096 || raw_size - 0x18 < (size_t)w * h * bpp / 8)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t need = (size_t)w * h * bpp / 8;

	char tmp[PATH_MAX], png[PATH_MAX], tmp_png[PATH_MAX];
	snprintf (tmp, sizeof (tmp), "%s.hvt-tmp.tpl", arg);
	snprintf (tmp_png, sizeof (tmp_png), "%s.png", tmp);
	snprintf (png, sizeof (png), "%s.png", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE HVT:%s -> PNG:%s\n", verbose > 0 ? "\n" : "",
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
	memcpy (tpl + data_off, raw + 0x18, need);
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
