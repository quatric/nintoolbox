// Next Level Games texture banks (Mario Strikers Charged, Wii): ".rlt" ("PTLG": u32 count @4, then count
// {u32 name hash, u32 offset, u32 size (top bits are flags), 0} records from 0x10, offsets relative to the end
// of the table) and the ".res" / ".dmn" resource files (u32 0x20 @0, u32 count @4, count {u32 name hash,
// u32 offset / 0x20, u32 size} records from 0x20). A texture is a 0x20-byte big-endian header {u32 mip
// levels, u32 kind (1 RGB5A3, 2 CMPR, 3 RGBA8), 4 colour-depth bytes @8, u16 width @0xe, u16 height @0x10}
// followed by GX-tiled levels, largest first. The base level of every texture is decoded to
// "<file>.<hash>.png" in decode mode (xx); entries that are not textures are skipped.
static uint nltex_level_bytes (uint kind, uint w, uint h)
{
	const uint bw = kind == 2 ? 8 : 4, bh = kind == 2 ? 8 : 4, bpt = kind == 2 ? 32 : kind == 1 ? 32 : 64;
	return (w + bw - 1) / bw * ((h + bh - 1) / bh) * bpt;
}

static bool nltex_entry (ccp arg, const u8 *p, size_t avail, uint hash)
{
	if (avail < 0x40)
		return false;
	const uint mips = rd_be32 (p), kind = rd_be32 (p + 4), w = rd_be16 (p + 0xe), h = rd_be16 (p + 0x10);
	if (mips < 1 || mips > 12 || kind < 1 || kind > 3 || !w || !h || w > 2048 || h > 2048 || (w & (w - 1)) || (h & (h - 1))
		|| rd_be16 (p + 0xc) || rd_be16 (p + 0x12))
		return false;
	const u8 *bits = p + 8;
	if (!((kind == 2 && !memcmp (bits, "\5\6\5", 3) && bits[3] <= 1) || (kind == 1 && !memcmp (bits, "\5\5\5\3", 4))
		|| (kind == 3 && !memcmp (bits, "\10\10\10\10", 4))))
		return false;
	size_t total = 0x20;
	for (uint m = 0, lw = w, lh = h; m < mips; m++, lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1)
		total += nltex_level_bytes (kind, lw, lh);
	if (total > avail)
		return false;
	u8 *rgba = eashpg_decode (kind == 2 ? 14 : kind == 1 ? 5 : 6, p + 0x20, avail - 0x20, w, h, 0, 0, 0);
	if (!rgba)
		return false;
	char png[PATH_MAX];
	snprintf (png, sizeof (png), "%s.%08x.png", arg, hash);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE NL-TEX:%s %08x %ux%u -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			arg, hash, w, h, png);
	if (testmode)
		FREE (rgba);
	else
		SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true);
	return true;
}

static enumError extract_nltex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if ((!is_ext (arg, ".rlt") && !is_ext (arg, ".res") && !is_ext (arg, ".dmn")) || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	uint n = 0, found = 0;
	if (sz > 0x40 && !memcmp (raw, "PTLG", 4))
	{
		n = rd_be32 (raw + 4);
		if (n && n < 0x10000 && 0x10 + (size_t)n * 16 <= sz)
		{
			const size_t base = 0x10 + (size_t)n * 16;
			for (uint i = 0; i < n; i++)
			{
				const u8 *e = raw + 0x10 + (size_t)i * 16;
				const size_t off = base + rd_be32 (e + 4), size = rd_be32 (e + 8) & 0x0fffffff;
				if (off <= sz && size <= sz - off && nltex_entry (arg, raw + off, size, rd_be32 (e)))
					found++;
			}
		}
	}
	else if (sz > 0x40 && rd_be32 (raw) == 0x20)
	{
		n = rd_be32 (raw + 4);
		if (n && n < 0x10000 && 0x20 + (size_t)n * 12 <= sz)
			for (uint i = 0; i < n; i++)
			{
				const u8 *e = raw + 0x20 + (size_t)i * 12;
				const size_t off = (size_t)rd_be32 (e + 4) * 0x20, size = rd_be32 (e + 8) & 0x0fffffff;
				if (off <= sz && size <= sz - off && nltex_entry (arg, raw + off, size, rd_be32 (e)))
					found++;
			}
	}
	FREE (raw);
	return found ? ERR_OK : ERR_NOTHING_TO_DO;
}
