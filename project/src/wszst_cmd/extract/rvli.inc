// WordJong Party "*.rvli" images: "rvli", "rvif" at +8, be32 1, width, height, bits per pixel (32 = RGBA32, 16 = RGB5A3) at +0x10..+0x20, "eimg" at +0x24, "rvpx" at +0x38 and the GX-tiled pixels from 0x40.
// The image is wrapped in a single-image TPL "<file>.tpl", which the TPL handler decodes to PNG.
static enumError extract_rvli_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".rvli"))
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
		if (memcmp (raw, "rvli", 4) || memcmp (raw + 8, "rvif", 4) || memcmp (raw + 0x38, "rvpx", 4) || be32 (raw + 0x10) != 1)
			goto done;
		const uint w = be32 (raw + 0x14), h = be32 (raw + 0x18), bpp = be32 (raw + 0x1c);
		uint fmt;
		if (bpp == 32)
			fmt = 6;
		else if (bpp == 16)
			fmt = 5;
		else
			goto done;
		if (!w || !h || w > 2048 || h > 2048 || (w & 3) || (h & 3))
			goto done;
		const size_t dl = (size_t)w * h * bpp / 8;
		if (0x40 + dl != (size_t)sz)
			goto done;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT RVLI:%s (%ux%u fmt %u)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, fmt);
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
