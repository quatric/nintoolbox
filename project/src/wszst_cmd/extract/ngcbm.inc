// "_NGCBM01" GameCube/Wii bitmap (Totally Spies! Totally Party, unpacked from BIGFILE DKAP members):
// 0x20-byte header {"_NGCBM01", u16 width, u16 height, u16 GX format, u8 mip count, u8 flags,
// u32 0, u32 data offset (0x20), ...} followed by the plain GX texel chain. Decoded to "<name>.png"
// in decode mode (xx) by wrapping the first level in a synthetic TPL.
static enumError extract_ngcbm_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;
	if (!is_ext (arg, ".ngcbm"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size < 0x20 || memcmp (raw, "_NGCBM01", 8))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint w = rd_be16 (raw + 8), h = rd_be16 (raw + 10), fmt = rd_be16 (raw + 12), off = rd_be32 (raw + 20);
	uint bw = 4, bh = 4, bytes_per_block = 32;
	switch (fmt)
	{
		case 0: case 14: bw = 8, bh = 8; break;           // I4, CMPR
		case 1: case 2: bw = 8, bh = 4; break;            // I8, IA4
		case 3: case 4: case 5: break;                    // IA8, RGB565, RGB5A3
		case 6: break;                                    // RGBA8
		default: bw = 0;
	}
	if (!bw || !w || !h || w > 4096 || h > 4096 || off < 0x20 || off > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t need = (size_t)((w + bw - 1) / bw) * ((h + bh - 1) / bh) * bytes_per_block;
	if (raw_size - off < need)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char tmp[PATH_MAX], png[PATH_MAX], tmp_png[PATH_MAX];
	snprintf (tmp, sizeof (tmp), "%s.ngcbm-tmp.tpl", arg);
	snprintf (tmp_png, sizeof (tmp_png), "%s.png", tmp);
	snprintf (png, sizeof (png), "%s.png", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE NGCBM:%s -> PNG:%s\n", verbose > 0 ? "\n" : "",
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
	PUT32 (fmt); PUT32 (data_off); PUT32 (0); PUT32 (0); PUT32 (1); PUT32 (1); PUT32 (0);
#undef PUT32
	memcpy (tpl + data_off, raw + off, need);
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
