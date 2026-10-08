// EA "TXG " texture groups (Tiger Woods PGA Tour course hole archives, the
// "txfh_*.bin" members). Big-endian chunks {tag, u32 size, data}: HEAD, TXHE
// (88-byte records), CLHE, TXDA (texture data). Record: +0x00 name[16],
// +0x10 four mip entries {u32 offset in TXDA, u16 blocks, u16 0xffff, u32 ?},
// +0x40 u16 width, u16 height, +0x48 u8 GX format (14 CMPR, 5 RGB5A3, 0 I4).
// The base level of each texture is decoded to "<file>.<NNN>_<name>.png"
// in decode mode (xx).
static enumError extract_txg_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x40 || memcmp (raw, "TXG ", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *he = 0, *da = 0;
	size_t he_size = 0, da_size = 0;
	for (size_t p = 8; p + 8 <= sz;)
	{
		const size_t cs = rd_be32 (raw + p + 4);
		if (cs > sz - p - 8)
			break;
		if (!memcmp (raw + p, "TXHE", 4))
			he = raw + p + 8, he_size = cs;
		else if (!memcmp (raw + p, "TXDA", 4))
			da = raw + p + 8, da_size = cs;
		p += 8 + cs;
	}
	if (!he || !da || he_size % 88)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_OK;
	uint done = 0;
	for (uint i = 0; i < he_size / 88 && !err; i++)
	{
		const u8 *r = he + 88 * i;
		const uint w = (r[0x40] << 8) | r[0x41], h = (r[0x42] << 8) | r[0x43], fmt = r[0x48];
		const size_t off = rd_be32 (r + 0x10);
		if (!w || !h || w > 2048 || h > 2048 || (w | h) & 7)
			continue;
		size_t need = 0;
		switch (fmt)
		{
		case 14: need = (size_t)w * h / 2; break;
		case 5: need = (size_t)w * h * 2; break;
		case 0: need = (size_t)w * h / 2; break;
		default: continue;
		}
		if (off > da_size || need > da_size - off)
			continue;
		const u8 *src = da + off;
		u8 *rgba = 0;
		if (fmt == 14)
			rgba = GxDecodeCmprAlpha4 (src, 0, w, h);
		else
		{
			rgba = MALLOC ((size_t)w * h * 4);
			if (!rgba)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			if (fmt == 5)
			{
				for (uint ty = 0; ty < h; ty += 4)
					for (uint tx = 0; tx < w; tx += 4)
						for (uint k = 0; k < 16; k++, src += 2)
						{
							const uint v = (src[0] << 8) | src[1];
							u8 *o = rgba + ((size_t)(ty + k / 4) * w + tx + k % 4) * 4;
							if (v & 0x8000)
							{
								o[0] = ((v >> 10) & 31) * 255 / 31;
								o[1] = ((v >> 5) & 31) * 255 / 31;
								o[2] = (v & 31) * 255 / 31;
								o[3] = 255;
							}
							else
							{
								o[0] = ((v >> 8) & 15) * 17;
								o[1] = ((v >> 4) & 15) * 17;
								o[2] = (v & 15) * 17;
								o[3] = ((v >> 12) & 7) * 255 / 7;
							}
						}
			}
			else
			{
				for (uint ty = 0; ty < h; ty += 8)
					for (uint tx = 0; tx < w; tx += 8)
						for (uint k = 0; k < 64; k++)
						{
							const uint nib = (k & 1) ? (src[k / 2] & 15) : (src[k / 2] >> 4);
							u8 *o = rgba + ((size_t)(ty + k / 8) * w + tx + k % 8) * 4;
							o[0] = o[1] = o[2] = o[3] = nib * 17;
						}
			}
		}
		if (!rgba)
			continue;
		char name[17];
		memcpy (name, r, 16);
		name[16] = 0;
		for (char *c = name; *c; c++)
			if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '_'))
				*c = '_';
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.%03u_%s.png", arg, i, name);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE TXG:%s[%u] %ux%u -> PNG:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, i, w, h, png);
		if (testmode)
			FREE (rgba);
		else
			err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true);
		done++;
	}
	FREE (raw);
	if (!err && !done)
		return ERR_NOTHING_TO_DO;
	return err;
}
