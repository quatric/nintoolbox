// Namco "Tales of" (Wii) texture sets: "*.txm" (big-endian directory {u32 0x20000, u32 size, u32 10, u32 count}, then count 0x1c-byte entries from 0x14 {u32 flags, u32 width, u32 height, u32 mip levels, u32 GX format (1=I8, 3=IA8, 6=RGBA8, 14=CMPR; flag bits above 16 are ignored), u32 name offset (relative to this field), u32 data offset}) and the matching "*.txv" pixel data
// (GX tiled; mip chain stored after the top level, only the top level is decoded). Each texture is wrapped into a single-image TPL "<name>.tpl" beside "<file>.d/" and decoded to PNG by the TPL handler. The same pair also appears as FPS4 members inside "*.slz" (see slz.inc).
static enumError txm_write_set (ccp arg, const u8 *txm, size_t txm_sz, const u8 *txv, size_t txv_sz, ccp dest, uint depth)
{
	if (txm_sz < 0x30 || rd_be32 (txm) != 0x20000)
		return ERR_NOTHING_TO_DO;
	const uint count = rd_be32 (txm + 12);
	if (!count || count > 4096 || 0x14 + (u64)count * 0x1c > txm_sz)
		return ERR_NOTHING_TO_DO;
	static const int bw[15] = { 8, 8, 8, 4, 4, 4, 4, 0, 8, 8, 4, 0, 0, 0, 8 }, bh[15] = { 8, 4, 4, 4, 4, 4, 4, 0, 8, 4, 4, 0, 0, 0, 8 };
	enumError err = ERR_OK;
	uint done = 0;
	for (uint i = 0; i < count && !err; i++)
	{
		const u8 *e = txm + 0x14 + (size_t)i * 0x1c;
		const uint w = rd_be32 (e + 4), h = rd_be32 (e + 8), fmt = rd_be32 (e + 16) & 0xffff;
		const u32 off = rd_be32 (e + 24), noff = rd_be32 (e + 20);
		if (!w || !h || w > 4096 || h > 4096 || fmt > 14 || !bw[fmt] || (fmt != 1 && fmt != 3 && fmt != 6 && fmt != 14 && fmt != 0 && fmt != 2 && fmt != 4 && fmt != 5))
			continue;
		const uint W = (w + bw[fmt] - 1) / bw[fmt] * bw[fmt], H = (h + bh[fmt] - 1) / bh[fmt] * bh[fmt];
		size_t len = (size_t)W * H;
		switch (fmt)
		{
			case 0: case 14: len /= 2; break;
			case 3: case 4: case 5: len *= 2; break;
			case 6: len *= 4; break;
		}
		if ((u64)off + len > txv_sz)
			continue;
		char name[64];
		uint k = 0;
		const size_t np = 0x14 + (size_t)i * 0x1c + 20 + noff;
		if (np < txm_sz)
			for (; k < 60 && np + k < txm_sz && txm[np + k] >= 0x20 && txm[np + k] < 0x7f; k++)
				name[k] = txm[np + k] == '/' || txm[np + k] == '\\' ? '_' : txm[np + k];
		if (!k)
			k = (uint)snprintf (name, sizeof (name), "tex_%03u", i);
		else
			name[k] = 0;
		u8 *tpl = MALLOC (0x60 + len);
		memset (tpl, 0, 0x60);
		wr_be32 (tpl, 0x0020af30); wr_be32 (tpl + 4, 1); wr_be32 (tpl + 8, 0x0c);
		wr_be32 (tpl + 0x0c, 0x14);
		tpl[0x14] = h >> 8; tpl[0x15] = h; tpl[0x16] = w >> 8; tpl[0x17] = w;
		wr_be32 (tpl + 0x18, fmt); wr_be32 (tpl + 0x1c, 0x60);
		wr_be32 (tpl + 0x28, 1); wr_be32 (tpl + 0x2c, 1);
		memcpy (tpl + 0x60, txv + off, len);
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s.tpl", dest, name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (tpl, 1, 0x60 + len, F.f) != 0x60 + len)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", 0x60 + len, path);
		ResetFile (&F, opt_preserve);
		FREE (tpl);
		done++;
		if (!err)
		{
			const enumError sub = extract_one_file (path, 0, depth + 1);
			if (sub != ERR_NOTHING_TO_DO && err < sub)
				err = sub;
		}
	}
	return err ? err : done ? ERR_OK : ERR_NOTHING_TO_DO;
}

static enumError extract_txm_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".txm"))
		return ERR_NOTHING_TO_DO;
	char vpath[PATH_MAX];
	snprintf (vpath, sizeof (vpath), "%s", arg);
	vpath[strlen (vpath) - 1] = 'v';
	u8 *a = 0, *b = 0;
	size_t as = 0, bs = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &a, &as, 0, 0, 0, false) || !a || LoadFileAlloc (vpath, 0, 0, &b, &bs, 0, 0, 0, false) || !b)
		goto done;
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT TXM:%s -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
		if (!testmode)
			err = txm_write_set (arg, a, as, b, bs, dest, depth);
		else
			err = ERR_OK;
	}
done:
	FREE (a);
	FREE (b);
	return err;
}
