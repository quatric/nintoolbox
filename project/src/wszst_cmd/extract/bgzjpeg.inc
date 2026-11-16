// Capcom "*.bgz" pre-rendered background packs (Resident Evil Archives: Resident Evil, Wii: "st*/r*.bgz"), big-endian:
//   a header of 0x20-byte entries {u32 header size (0x20 * entries), u32 w1, u32 w2, u32 w3, u32 w4, u32 1, 0, 0} followed by, per camera angle, a few
//   small binary blocks (float data, "shd." tables) and one or more JFIF JPEG images (the background pictures, often 2 per block, the second tiny).
// The binary block layout is not decoded; the file is carved losslessly into consecutive pieces: "<name>_NNN.jpg" for every complete JPEG
// stream and "<name>_NNN.bin" for the bytes between them (the header comes first as a .bin).
static size_t bgz_jpeg_len (const u8 *d, size_t sz, size_t p)
{
	size_t q = p + 2;
	while (q + 4 <= sz)
	{
		if (d[q] != 0xff)
			return 0;
		const u8 m = d[q + 1];
		if (m == 0xd9)
			return q + 2 - p;
		if (m == 0xff)
		{
			q++;
			continue;
		}
		if (m == 0x01 || (m >= 0xd0 && m <= 0xd8))
		{
			q += 2;
			continue;
		}
		const size_t l = d[q + 2] << 8 | d[q + 3];
		if (l < 2)
			return 0;
		q += 2 + l;
		if (m == 0xda)
		{
			// entropy coded data up to the next real marker
			while (q + 1 < sz && !(d[q] == 0xff && d[q + 1] != 0 && !(d[q + 1] >= 0xd0 && d[q + 1] <= 0xd7)))
				q++;
		}
	}
	return 0;
}

static enumError extract_bgzjpeg_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".bgz"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const u32 hs = sz >= 32 ? rd_be32 (raw) : 0;
	bool good = hs >= 0x20 && hs <= 0x400 && !(hs % 0x20) && hs < sz;
	for (u32 e = 0; good && e < hs / 0x20; e++)
		if (rd_be32 (raw + e * 0x20 + 20) != 1 || rd_be32 (raw + e * 0x20 + 24) || rd_be32 (raw + e * 0x20 + 28))
			good = false;
	// piece list: {offset, length, is_jpeg}
	enum { MAXP = 4096 };
	size_t *po = good ? MALLOC (sizeof (size_t) * 3 * MAXP) : 0;
	uint np = 0;
	if (good && po)
	{
		size_t cur = 0, p = hs;
		po[0] = 0, po[1] = hs, po[2] = 0;
		np = 1;
		cur = hs;
		while (p + 4 < sz && np + 2 < MAXP)
		{
			if (raw[p] == 0xff && raw[p + 1] == 0xd8 && raw[p + 2] == 0xff)
			{
				const size_t l = bgz_jpeg_len (raw, sz, p);
				if (l)
				{
					if (p > cur)
					{
						po[np * 3] = cur, po[np * 3 + 1] = p - cur, po[np * 3 + 2] = 0;
						np++;
					}
					po[np * 3] = p, po[np * 3 + 1] = l, po[np * 3 + 2] = 1;
					np++;
					p += l;
					cur = p;
					continue;
				}
			}
			p++;
		}
		if (sz > cur)
		{
			po[np * 3] = cur, po[np * 3 + 1] = sz - cur, po[np * 3 + 2] = 0;
			np++;
		}
		uint nj = 0;
		for (uint i = 0; i < np; i++)
			nj += po[i * 3 + 2] ? 1 : 0;
		if (!nj)
			good = false;
	}
	else
		good = false;
	if (good)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT BGZ:%s (%u pieces) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, np, dest);
		err = ERR_OK;
		for (uint i = 0; i < np && !err && !testmode; i++)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%03u.%s", dest, basedir ? basedir : "", i, po[i * 3 + 2] ? "jpg" : "bin");
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (raw + po[i * 3], 1, po[i * 3 + 1], F.f) != po[i * 3 + 1])
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)po[i * 3 + 1], path);
			ResetFile (&F, opt_preserve);
		}
	}
	FREE (po);
	FREE (raw);
	return err;
}
