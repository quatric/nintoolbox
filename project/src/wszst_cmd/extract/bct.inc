// Nicktoons MLB "*.bct" (stadium environment maps): big-endian 0x30-byte header {u8 6, ..., u16 width @4, u16 height @6, ..., u32 data size @0x1c} followed by a CMPR (DXT1-style) texture with its mip chain.
// The top mip is wrapped in a single-image TPL "<file>.tpl" and decoded to PNG by the TPL handler.
static enumError extract_bct_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".bct"))
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
		if (sz < 0x30 + 32 || sz > 0x4000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		raw = MALLOC ((size_t)sz);
		if (fread (raw, 1, (size_t)sz, f) != (size_t)sz || raw[0] != 6)
			goto done;
		const uint w = be16 (raw + 4), h = be16 (raw + 6), len = be32 (raw + 0x1c);
		const size_t dl = (size_t)w * h / 2;
		if (w < 8 || h < 8 || (w & 7) || (h & 7) || w > 2048 || h > 2048 || len < dl || 0x30 + dl > (size_t)sz)
			goto done;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT BCT:%s (%ux%u CMPR)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w, h);
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
			wr_be32 (tpl + 0x18, 14);
			wr_be32 (tpl + 0x1c, 0x60);
			wr_be32 (tpl + 0x28, 1);
			wr_be32 (tpl + 0x2c, 1);
			memcpy (tpl + 0x60, raw + 0x30, dl);
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
