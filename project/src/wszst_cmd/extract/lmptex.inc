// Jillian Michaels' Fitness Ultimatum (Wii) "resfbc_wii/*.lmp" texture lumps (the sibling ".lps" is an XML build manifest and is ignored), big-endian: {u16 count, u16 1}, count x 24-byte
// entries {u32 0, u32 name offset, u32 0, u32 data offset, u32 data size, u32 0}, NUL-terminated names ("*.tex"), and per entry a 0x80-byte header plus GX pixel data. The header stores the
// width and height as big-endian u16 at +0/+2 and a format word at +6 (0x8080 = GX RGBA8, 0x0004 = GX CMPR, a single level). Entries with another format word are skipped. Each texture is written to "<file>.d/<name>.png".
static enumError extract_lmptex_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".lmp"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	const uint n = sz > 0x40 ? rd_be16 (raw) : 0;
	if (!n || n > 4096 || rd_be16 (raw + 2) != 1 || 4 + (u64)n * 24 > sz)
		goto done;
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		uint done_count = 0;
		for (uint i = 0; i < n; i++)
		{
			const u8 *e = raw + 4 + 24 * i;
			const u32 noff = rd_be32 (e + 4), off = rd_be32 (e + 12), size = rd_be32 (e + 16);
			if (noff >= sz || off + (u64)size > sz || size <= 0x80 || rd_be32 (e) || rd_be32 (e + 8))
				continue;
			const u8 *h = raw + off;
			const uint w = rd_be16 (h), ht = rd_be16 (h + 2), tail = rd_be16 (h + 6);
			const uint fmt = tail == 0x8080 ? 6 : tail == 0x0004 ? 14 : 99;
			if (fmt == 99 || !w || !ht)
				continue;
			const u64 pix = size - 0x80;
			if (pix != (fmt == 6 ? (u64)w * ht * 4 : (u64)((w + 7) / 8) * ((ht + 7) / 8) * 32))
				continue;
			char name[256];
			snprintf (name, sizeof (name), "%.200s", (ccp)raw + noff);
			char png[PATH_MAX];
			snprintf (png, sizeof (png), "%s/%s%s.png", dest, basedir ? basedir : "", name);
			if (!done_count && (verbose >= 0 || testmode))
				fprintf (stdlog, "%s%sEXTRACT LMP:%s (%u textures) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
			done_count++;
			if (testmode)
			{
				err = ERR_OK;
				continue;
			}
			{
				File_t F;
				CreateFileOpt (&F, true, png, false, arg);
				ResetFile (&F, false);
				unlink (png);
			}
			err = wiitex_write_raw (png, w, ht, fmt, raw + off + 0x80, (uint)pix);
			if (err)
				break;
		}
		if (!done_count)
			goto done;
		if (!err)
			err = ERR_OK;
	}
done:
	FREE (raw);
	return err;
}
