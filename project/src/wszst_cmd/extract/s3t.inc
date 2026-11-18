// Planet 51 "*.s3t" textures: big-endian 0x20-byte header {u32 GX format, u32 width, u32 height, 0, 0, 0, u32 flags (0x201), 0} followed by the GX-tiled top-level image (no mips).
// The image is wrapped in a single-image TPL "<file>.tpl", which the TPL handler decodes to PNG.
static enumError extract_s3t_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".s3t"))
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
		if (sz < 0x40 || sz > 0x4000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		raw = MALLOC ((size_t)sz);
		if (fread (raw, 1, (size_t)sz, f) != (size_t)sz)
			goto done;
		const uint fmt = be32 (raw), w = be32 (raw + 4), h = be32 (raw + 8);
		uint bw, bh, bsz;
		switch (fmt)
		{
		case 0: bw = 8; bh = 8; bsz = 32; break;
		case 1: case 2: bw = 8; bh = 4; bsz = 32; break;
		case 3: case 4: case 5: bw = 4; bh = 4; bsz = 32; break;
		case 6: bw = 4; bh = 4; bsz = 64; break;
		case 14: bw = 8; bh = 8; bsz = 32; break;
		default: goto done;
		}
		if (!w || !h || w > 2048 || h > 2048 || be32 (raw + 12) || be32 (raw + 16))
			goto done;
		const size_t dl = (size_t)((w + bw - 1) / bw) * ((h + bh - 1) / bh) * bsz;
		if (0x20 + dl > (size_t)sz)
			goto done;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT S3T:%s (%ux%u fmt %u)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h, fmt);
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
			memcpy (tpl + 0x60, raw + 0x20, dl);
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
