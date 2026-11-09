// Pangya "gsr" resource (Super Swing Golf / Pangya Wii ".gsr" textures): big endian {u32 0x20, "ffrg" "\0rsg" (a byte-swapped "grff" "gsr\0"), u32 file
// size, u32 ?, u32 chunk count, ...}; 16 byte chunk headers from 0x20 {tag stored reversed ("\0xet" = "tex", "dxet" = "texd", "\0lap" = "pal",
// "dlap" = "pald"), u32 a, u32 b, u32 c}. "tex": a records of b bytes at c, {u32 format (0x80000000 | GX format: 6 RGBA8, 9 C8, 0xe CMPR), u32 width, u32
// height, u32 levels, u32 level-0 size, u32 0}. "texd": a bytes of pixel data at c. "pal": records {u32 format 6, u32 entries, 0}, "pald": 32 bit ARGB palette
// of a bytes at c. Written as "<file>.png" (level 0 only).
static bool gsr_chunk (const u8 *raw, size_t sz, uint nch, const char *tag, u32 *a, u32 *b, u32 *c)
{
	for (uint i = 0; i < nch; i++)
	{
		const u8 *h = raw + 0x20 + 16 * (size_t)i;
		if (h[0] == tag[3] && h[1] == tag[2] && h[2] == tag[1] && h[3] == tag[0])
		{
			*a = rd_be32 (h + 4), *b = rd_be32 (h + 8), *c = rd_be32 (h + 12);
			return (u64)*c <= sz;
		}
	}
	return false;
}

static enumError extract_gsr_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".gsr") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *rgba = 0;
	if (sz < 0x60 || rd_be32 (raw) != 0x20 || memcmp (raw + 4, "ffrg\0rsg", 8))
		goto done;
	{
		const uint nch = rd_be32 (raw + 0x14);
		u32 ta, tb, tc, da, db, dc;
		if (nch < 2 || nch > 16 || 0x20 + 16 * (size_t)nch > sz || !gsr_chunk (raw, sz, nch, "tex\0", &ta, &tb, &tc)
			|| !gsr_chunk (raw, sz, nch, "texd", &da, &db, &dc) || !ta || tb < 0x14 || (u64)tc + tb > sz || (u64)dc + da > sz)
			goto done;
		const u8 *tr = raw + tc;
		const uint fmt = rd_be32 (tr) & 0xff, w = rd_be32 (tr + 4), h = rd_be32 (tr + 8);
		if (!w || !h || w > 4096 || h > 4096)
			goto done;
		const uint bw = fmt == 0 || fmt == 1 || fmt == 2 || fmt == 14 || fmt == 9 ? 8 : 4;
		const uint bh = fmt == 0 || fmt == 14 ? 8 : 4;
		const uint pw = (w + bw - 1) / bw * bw, ph = (h + bh - 1) / bh * bh;
		uint dfmt = fmt, bits;
		if (fmt == 9)
			dfmt = 1, bits = 8;
		else if (fmt == 6)
			bits = 32;
		else if (fmt == 14)
			bits = 4;
		else
			goto done;
		const size_t need = (size_t)pw * ph * bits / 8;
		if (need > da)
			goto done;
		u32 pa = 0, pb, pc;
		if (fmt == 9 && (!gsr_chunk (raw, sz, nch, "pald", &pa, &pb, &pc) || pa < 4 * 256))
			goto done;
		if (DecodeGXTexture_RGBA (&rgba, pw, ph, dfmt, raw + dc, (uint)need, 0, 0, 0) || !rgba)
			goto done;
		if (fmt == 9)
			for (size_t i = 0; i < (size_t)pw * ph; i++)
			{
				const u8 *e = raw + pc + 4 * (size_t)rgba[4 * i];
				rgba[4 * i] = e[1], rgba[4 * i + 1] = e[2], rgba[4 * i + 2] = e[3], rgba[4 * i + 3] = e[0];
			}
		if (pw != w)
			for (uint y = 1; y < h; y++)
				memmove (rgba + (size_t)y * w * 4, rgba + (size_t)y * pw * 4, (size_t)w * 4);
		char dest[PATH_MAX];
		beside_source_dest_ext (dest, sizeof (dest), arg, ".png");
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT GSR:%s -> PNG:%s\n", arg, dest);
			FREE (rgba);
			rgba = 0;
			err = ERR_OK;
			goto done;
		}
		err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, dest, 0, true);
		rgba = 0;
		if (!err && verbose >= 0)
			fprintf (stdlog, "%sEXTRACT GSR:%s (%ux%u fmt %u) -> PNG:%s\n", verbose > 0 ? "\n" : "", arg, w, h, fmt, dest);
	}
done:
	FREE (raw);
	return err;
}
