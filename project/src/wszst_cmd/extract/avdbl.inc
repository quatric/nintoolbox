// Avalanche Software ".dbl" / ".mdb" resource databases (Disney's Bolt, Wii; big endian). A 0x20 byte file
// header ("GCN\0" platform tag at +8, version 0x100 at +0x10) is followed by chunks {u16 type, u16 flags,
// u32 size, u16 version, ...}: the header is 10 bytes, 0x2a with flag 2, 0x20 with flag 8 (0x40 with both),
// then size payload bytes. The DB_TEXTURE_SET chunk (type 0x82) payload D starts with the texture count,
// and 0x48-byte records from D+0x18: char name[0x20] (a ".tga" path), u32 GX format (| 0x8000), 2 unused,
// u32 pixel offset (from D), u16 width, u16 height, 2 unused, u32 palette offset (from D), u16 palette
// width, u16 palette height (entries = w * h, RGB565), 1 unused. Base levels are written as
// "<file>.<NN>.png" in decode mode (xx).
static enumError extract_avdbl_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if ((!is_ext (arg, ".dbl") && !is_ext (arg, ".mdb")) || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || rd_be32 (raw + 0x10) != 0x100)
		goto done;
	uint nout = 0;
	for (size_t p = 0x20; p + 10 <= sz;)
	{
		const uint type = rd_be16 (raw + p), fl = rd_be16 (raw + p + 2);
		const size_t size = rd_be32 (raw + p + 4);
		const size_t hl = fl & 8 ? (fl & 2 ? 0x40 : 0x20) : (fl & 2 ? 0x2a : 10);
		if (p + hl > sz || size > sz - p - hl)
			break;
		const size_t D = p + hl;
		if (type == 0x82 && size >= 0x60)
		{
			const uint n = rd_be32 (raw + D);
			for (uint i = 0; i < n && i < 1024 && 0x18 + 0x48 * (size_t)(i + 1) <= size; i++)
			{
				const u8 *r = raw + D + 0x18 + 0x48 * (size_t)i;
				const uint fmt = rd_be32 (r + 0x20) & 0xff, w = rd_be16 (r + 0x30), h = rd_be16 (r + 0x32);
				const size_t po = rd_be32 (r + 0x2c), pal = rd_be32 (r + 0x3c);
				const uint pw = rd_be16 (r + 0x40), ph = rd_be16 (r + 0x42), pn = pw * ph;
				if (!w || !h || w > 2048 || h > 2048 || po >= size)
					continue;
				const bool paletted = fmt == 8 || fmt == 9;
				if (paletted && (!pn || pn > 256 || pal + 2 * (size_t)pn > size))
					continue;
				u8 *rgba = eashpg_decode (fmt, raw + D + po, size - po, w, h, paletted ? raw + D + pal : 0, pn, 1);
				if (!rgba)
					continue;
				char name[40], suffix[64];
				memcpy (name, r, 0x20);
				name[0x20] = 0;
				const char *b = strrchr (name, '\\');
				b = b ? b + 1 : name;
				snprintf (suffix, sizeof (suffix), ".%02u.%.24s", i, b);
				for (char *q = suffix; *q; q++)
					if (*q == '/' || (u8)*q < 0x20)
						*q = '_';
				char *dot = strrchr (suffix, '.');
				if (dot && dot > suffix + 3 && !strcasecmp (dot, ".tga"))
					*dot = 0;
				char png[PATH_MAX];
				snprintf (png, sizeof (png), "%s%s.png", arg, suffix);
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sDECODE AV-DBL:%s %ux%u fmt %u -> PNG:%s\n", verbose > 0 ? "\n" : "",
						testmode ? "WOULD " : "", arg, w, h, fmt, png);
				nout++;
				if (testmode)
					FREE (rgba);
				else if ((err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true)) != ERR_OK)
					goto done;
			}
		}
		p = D + size;
	}
	err = nout ? ERR_OK : ERR_NOTHING_TO_DO;
done:
	FREE (raw);
	return err;
}
