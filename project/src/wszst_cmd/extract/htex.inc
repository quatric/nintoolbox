// Sega / Red Entertainment "HTEX" textures (Sakura Wars: So Long, My Love, Wii; ".htx"). A PS2-lineage 8-bit
// texture: "HTEX" header (0x10 bytes), "HTSF" (0x10), then a "PVRT" block {"PVRT", u32 size, u8 pixel format 9,
// u8 data format 0x6c, u16 0, u16 width, u16 height (little-endian)} followed by a 256-entry RGBA palette (alpha
// 0x80 = opaque, PS2 CLUT order: entries 8..15 and 16..23 of every 32 are swapped) and width * height 8-bit
// indices in PS2 GS PSMT8 block-swizzled order. Written to "<file>.png" in decode mode (xx). The "GBIX" flavour
// (unpalettised wipe masks) is left alone.
static enumError extract_htex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".htx") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint w = sz > 0x30 ? rd_le16 (raw + 0x2c) : 0, h = sz > 0x30 ? rd_le16 (raw + 0x2e) : 0;
	if (sz < 0x30 + 0x400 || memcmp (raw, "HTEX", 4) || memcmp (raw + 0x10, "HTSF", 4) || memcmp (raw + 0x20, "PVRT", 4)
		|| raw[0x28] != 9 || raw[0x29] != 0x6c || !w || !h || (w & 15) || (h & 15) || w > 2048 || h > 2048
		|| sz < 0x30 + 0x400 + (size_t)w * h)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *pal = raw + 0x30, *px = raw + 0x30 + 0x400;
	u8 *rgba = MALLOC ((size_t)w * h * 4);
	if (!rgba)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	for (uint y = 0; y < h; y++)
		for (uint x = 0; x < w; x++)
		{
			const size_t bl = (size_t)(y & ~15u) * w + (size_t)(x & ~15u) * 2;
			const uint sw = ((y + 2) >> 2 & 1) * 4, py = (((y & ~3u) >> 1) + (y & 1)) & 7;
			const size_t cl = (size_t)py * w * 2 + ((x + sw) & 7) * 4;
			const uint bn = ((y >> 1) & 1) + ((x >> 2) & 2);
			uint idx = px[bl + cl + bn];
			const uint blk = idx >> 3 & 3; // PS2 CLUT order
			if (blk == 1)
				idx += 8;
			else if (blk == 2)
				idx -= 8;
			const u8 *c = pal + 4 * idx;
			u8 *o = rgba + ((size_t)y * w + x) * 4;
			o[0] = c[0], o[1] = c[1], o[2] = c[2], o[3] = c[3] >= 0x80 ? 255 : (u8)(c[3] * 2);
		}
	FREE (raw);
	char png[PATH_MAX];
	snprintf (png, sizeof (png), "%s.png", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE HTEX:%s %ux%u -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg,
			w, h, png);
	if (testmode)
	{
		FREE (rgba);
		return ERR_OK;
	}
	return SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true);
}
