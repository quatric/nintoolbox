// Black Sheep Studio ".bgc" (Happy Neuron Academy DataBase/**/*.bgc; big endian): {0xfeedfeed, u32 1, u32 count, u32 table size (28 * count), u16 1,
// ".Black-Sheep." NUL @0x12}, then from 0x20 `count` 28-byte records {u32 id (hash), u32 width, u32 height, u32 size (= width * height * 4), u32 file
// offset, u32 12 (13 in Loading.bgc), u32 2 (0 in Loading.bgc)}. Each image is a GX RGBA8 texture (4x4 tiles, AR then GB planes); frames are spaced
// size + 0x800 apart. Written as "<file>.d/NNNN.png". Meaning of the last two fields is unknown.
static enumError extract_bgc_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".bgc") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || rd_be32 (raw) != 0xfeedfeed || rd_be32 (raw + 4) != 1 || memcmp (raw + 0x12, ".Black-Sheep.", 14))
		goto done;
	{
		const uint n = rd_be32 (raw + 8);
		if (!n || n > 100000 || rd_be32 (raw + 12) != 28 * n || 0x20 + 28 * (u64)n > sz)
			goto done;
		uint ok = 0;
		for (uint i = 0; i < n; i++)
		{
			const u8 *r = raw + 0x20 + 28 * i;
			const u32 w = rd_be32 (r + 4), h = rd_be32 (r + 8), sz1 = rd_be32 (r + 12), off = rd_be32 (r + 16);
			if (!w || !h || w > 4096 || h > 4096 || (w | h) & 3 || sz1 != w * h * 4 || (u64)off + sz1 > sz)
				goto done;
			ok++;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT BGC:%s (%u images) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ok, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !testmode && !err; i++)
		{
			const u8 *r = raw + 0x20 + 28 * i;
			const u32 w = rd_be32 (r + 4), h = rd_be32 (r + 8), sz1 = rd_be32 (r + 12), off = rd_be32 (r + 16);
			u8 *rgba = 0;
			if (DecodeGXTexture_RGBA (&rgba, w, h, 6, raw + off, sz1, 0, 0, 0) || !rgba)
			{
				FREE (rgba);
				continue;
			}
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%04u.png", dest, basedir ? basedir : "", i);
			err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true);
		}
	}
done:
	FREE (raw);
	return err;
}
