// Tony Hawk: Ride "*.txw" frontend images: 0x40-byte header {"\xaf\xaf\x00\x02", ..., 0xbeefface at +0x0c, u16 1, u16 type at +0x10 (2 = CMPR, 4 = 8-bit intensity), u16 width, u16 height at +0x14, ...} followed by the GX-tiled image.
// The image is wrapped in a single-image TPL "<file>.tpl", which the TPL handler decodes to PNG.
static enumError extract_txw_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".txw"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const long sz = ftell (f);
		if (sz < 0x80 || sz > 0x4000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		raw = MALLOC ((size_t)sz);
		if (fread (raw, 1, (size_t)sz, f) != (size_t)sz)
			goto done;
		if (be32 (raw) != 0xafaf0002 || be32 (raw + 0x0c) != 0xbeefface)
			goto done;
		const uint w = (raw[0x14] << 8) | raw[0x15], h = (raw[0x16] << 8) | raw[0x17];
		uint fmt, bw, bh, bsz;
		switch ((raw[0x12] << 8) | raw[0x13])
		{
		case 2: fmt = 14; bw = 8; bh = 8; bsz = 32; break;
		case 4: fmt = 14; bw = 8; bh = 8; bsz = 32; break;
		default: goto done;
		}
		if (!w || !h || w > 2048 || h > 2048)
			goto done;
		const size_t dl = (size_t)((w + bw - 1) / bw) * ((h + bh - 1) / bh) * bsz;
		if (0x40 + dl > (size_t)sz)
			goto done;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT TXW:%s (%ux%u fmt %u)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, fmt);
		err = ERR_OK;
		if (!testmode)
		{
			u8 *tpl = MALLOC (0x60 + dl);
			memset (tpl, 0, 0x60);
			wr_be32 (tpl, 0x0020af30);
			wr_be32 (tpl + 4, 1);
			wr_be32 (tpl + 8, 0x0c);
			wr_be32 (tpl + 0x0c, 0x14);
			tpl[0x14] = h >> 8;
			tpl[0x15] = h;
			tpl[0x16] = w >> 8;
			tpl[0x17] = w;
			wr_be32 (tpl + 0x18, fmt);
			wr_be32 (tpl + 0x1c, 0x60);
			wr_be32 (tpl + 0x28, 1);
			wr_be32 (tpl + 0x2c, 1);
			memcpy (tpl + 0x60, raw + 0x40, dl);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s.tpl", arg);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (tpl, 1, 0x60 + dl, F.f) != 0x60 + dl)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", 0x60 + dl, path);
			ResetFile (&F, opt_preserve);
			FREE (tpl);
			if (!err)
			{
				const enumError sub = extract_one_file (path, 0, depth + 1);
				if (sub != ERR_NOTHING_TO_DO && err < sub)
					err = sub;
			}
		}
	}
done:
	FREE (raw);
	fclose (f);
	return err;
}
