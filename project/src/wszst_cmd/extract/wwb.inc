// Marines: Modern Urban Combat "*.wwb" sound banks: little-endian {u32 count} then count * {u32 offset, u32 size}; members (28-byte header {u32, u32 samples, u32 rate, 0, 2, u32, 2} + ADPCM data) are written as "<file>.d/NNNN.snd" without decoding the audio.
static enumError extract_wwb_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".wwb"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *tab = 0, *buf = 0;
	u8 h[4];
	if (fread (h, 1, 4, f) != 4 || fseeko (f, 0, SEEK_END))
		goto done;
	{
		const u64 fsz = (u64)ftello (f);
		const uint n = rd_le32 (h);
		if (!n || n > 100000 || 4 + (u64)n * 8 > fsz)
			goto done;
		tab = MALLOC ((size_t)n * 8);
		if (fseeko (f, 4, SEEK_SET) || fread (tab, 1, (size_t)n * 8, f) != (size_t)n * 8)
			goto done;
		u64 prev_end = 4 + (u64)n * 8;
		for (uint i = 0; i < n; i++)
		{
			const u64 o = rd_le32 (tab + i * 8), s = rd_le32 (tab + i * 8 + 4);
			if (o < prev_end || o > prev_end + 16 || o + s > fsz)
				goto done;
			prev_end = o + s;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT WWB:%s (%u sounds) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		size_t cap = 0;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u32 off = rd_le32 (tab + i * 8), size = rd_le32 (tab + i * 8 + 4);
			if (!size)
				continue;
			if (size > cap)
			{
				FREE (buf);
				cap = size;
				buf = MALLOC (cap);
			}
			if (fseeko (f, off, SEEK_SET) || fread (buf, 1, size, f) != size)
				continue;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%04u.snd", dest, i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (buf, 1, size, F.f) != size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (tab);
	FREE (buf);
	fclose (f);
	return err;
}
