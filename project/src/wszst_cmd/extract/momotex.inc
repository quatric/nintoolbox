// Momotarou Dentetsu 16 "TA" texture members (the members of data/field_tex_*.bin; big endian): textures stored back to back
// (each padded to 0x20), each {"TA", u16 1, u32 size, u32 format (4 = C4, 5 = C8), u32 width, u32 height, u32 mip count, 2 zero words,
// RGB5A3 palette (16 or 256 entries), GX tiled indices (8x8 tiles for C4, 8x4 for C8), trailer}. One PNG per texture
// ("<file>.png", or "<file>.NN.png" when the file holds several).
static enumError extract_momotex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 hd[4];
	const size_t got = fread (hd, 1, 4, f);
	fclose (f);
	if (got != 4 || memcmp (hd, "TA\0\1", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x40 || sz > 0x10000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	uint count = 0;
	for (size_t p = 0; p + 0x40 <= sz && !memcmp (raw + p, "TA\0\1", 4); count++)
	{
		const size_t s = rd_be32 (raw + p + 4);
		if (s < 0x40 || s > sz - p)
			break;
		p += (s + 0x1f) & ~(size_t)0x1f;
	}
	if (!count)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_OK;
	uint k = 0;
	size_t p = 0;
	for (; k < count && !err; k++)
	{
		const size_t s = rd_be32 (raw + p + 4);
		const uint fmt = rd_be32 (raw + p + 8), w = rd_be32 (raw + p + 12), h = rd_be32 (raw + p + 16);
		const uint pn = fmt == 4 ? 16 : 256;
		const size_t dsz = fmt == 4 ? (size_t)w * h / 2 : (size_t)w * h;
		const size_t next = p + ((s + 0x1f) & ~(size_t)0x1f);
		if ((fmt != 4 && fmt != 5) || w < 8 || h < 8 || w > 4096 || h > 4096 || 0x20 + 2 * pn + dsz > s)
			break;
		u8 *rgba = 0;
		const enumError de = DecodeGXTexture_RGBA (&rgba, w, h, fmt == 4 ? 8 : 9, raw + p + 0x20 + 2 * pn, (uint)dsz, raw + p + 0x20, pn, 2);
		if (de || !rgba)
		{
			FREE (rgba);
			break;
		}
		char dest[PATH_MAX];
		if (count > 1)
			snprintf (dest, sizeof (dest), "%s.%02u.png", arg, k);
		else
			beside_source_dest_ext (dest, sizeof (dest), arg, ".png");
		if (testmode)
		{
			FREE (rgba);
			fprintf (stdlog, "WOULD EXTRACT MOMO-TA:%s -> PNG:%s\n", arg, dest);
		}
		else
		{
			err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, dest, 0, true);
			if (!err && verbose >= 0)
				fprintf (stdlog, "%sEXTRACT MOMO-TA:%s -> PNG:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
		p = next;
	}
	FREE (raw);
	return k ? err : ERR_NOTHING_TO_DO;
}

// Momotarou Dentetsu 2010 "data/font.bin", "font2.bin", "kantei.bin" (1179648 bytes): one 1024x1152 GX I8 texture (8x4 tiles) holding a 32x32 pixel
// glyph sheet (32 columns x 36 rows); Momotarou Dentetsu 16 (524288 bytes, loaded by the game as a 1024x1024 texture): 1024x1024 GX I4 (8x8 tiles); written as "<file>.png". The text in "message.bin" indexes these glyphs.
static enumError extract_momofont_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	if (strncmp (base, "font", 4) && strncmp (base, "kantei", 6))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || (sz != 1024 * 1152 && sz != 1024 * 1024 / 2))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const bool i4 = sz == 1024 * 1024 / 2; // Momotarou Dentetsu 16: 1024x1024 I4
	const uint fh = i4 ? 1024 : 1152;
	u8 *rgba = 0;
	const enumError de = DecodeGXTexture_RGBA (&rgba, 1024, fh, i4 ? 0 : 1, raw, (uint)sz, 0, 0, 0);
	FREE (raw);
	if (de || !rgba)
	{
		FREE (rgba);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".png");
	if (testmode)
	{
		FREE (rgba);
		fprintf (stdlog, "WOULD EXTRACT MOMO-FONT:%s -> PNG:%s\n", arg, dest);
		return ERR_OK;
	}
	const enumError err = SaveDecodedRGBAToPNG (rgba, 1024, fh, &be_func, dest, 0, true);
	if (!err && verbose >= 0)
		fprintf (stdlog, "%sEXTRACT MOMO-FONT:%s -> PNG:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
