// Family Game Night 4 "blockfiles/*.blk": 0x20 byte header {be16 1, be16 1, be16 file count n, ...}, then n entries of 0x9c bytes {char name[0x80] (path with '/'), be32 offset, be32 size, be32 flags, 16 byte hash}. The data starts at the table end rounded up to 0x20, members follow each other (0x20 aligned) and end at the end of the file. Members are written to "<file>.d/<name>".
static enumError extract_fgnblk_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".blk"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *d = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const long sz = ftell (f);
		if (sz < 0x100 || sz > 0x40000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz)
			goto done;
		const uint n = (d[4] << 8) | d[5];
		if (d[0] || d[1] != 1 || d[2] || d[3] != 1 || !n || 0x20 + 0x9c * (size_t)n > (size_t)sz)
			goto done;
		u64 next = (0x20 + 0x9c * (u64)n + 0x1f) & ~(u64)0x1f;
		for (uint i = 0; i < n; i++)
		{
			const u8 *e = d + 0x20 + 0x9c * i;
			const u32 o = be32 (e + 0x80), s = be32 (e + 0x84);
			if (o != next || (u64)o + s > (u64)sz || !e[0] || e[0x7f])
				goto done;
			next = ((u64)o + s + 0x1f) & ~(u64)0x1f;
		}
		if (next != (u64)sz)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT FGNBLK:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = d + 0x20 + 0x9c * i;
			const u32 o = be32 (e + 0x80), s = be32 (e + 0x84);
			ccp nm = (ccp)e;
			while (*nm == '/' || *nm == '\\')
				nm++;
			char rel[0x81];
			uint k = 0;
			for (; *nm && k < 0x80; nm++)
				rel[k++] = *nm == '\\' ? '/' : *nm;
			rel[k] = 0;
			if (!k || strstr (rel, ".."))
				snprintf (rel, sizeof (rel), "file%04u", i);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s", dest, rel);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && s && fwrite (d + o, 1, s, F.f) != s)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}
