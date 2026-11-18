// Samurai Shodown Anthology "*.acx" sound banks: be32 0, be32 count n, n entries {be32 offset, be32 size} from +8; the data follows the table and ends at (about) the end of the file. Members are written to "<file>.d/NNN.bin".
static enumError extract_acx_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".acx"))
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
		if (sz < 0x20 || sz > 0x20000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz)
			goto done;
		const u32 n = be32 (d + 4);
		if (be32 (d) || !n || n > 4096 || 8 + 8 * (size_t)n > (size_t)sz)
			goto done;
		u64 next = 8 + 8 * (u64)n, end = 0;
		for (uint i = 0; i < n; i++)
		{
			const u32 o = be32 (d + 8 + 8 * i), s = be32 (d + 12 + 8 * i);
			if (o < next || o > next + 0xf || (u64)o + s > (u64)sz + 0xf)
				goto done;
			next = (u64)o + s;
			end = next;
		}
		if (end + 0x10 < (u64)sz)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT ACX:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u32 o = be32 (d + 8 + 8 * i);
			u32 s = be32 (d + 12 + 8 * i);
			if ((u64)o + s > (u64)sz)
				s = (u32)(sz - o);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%03u.bin", dest, i);
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

// Detective Conan (Korea) "*.hst": "HST\0", be32 1, be32 image data size, then a plain TPL (0x0020af30) from +0x0c. The TPL is written to "<file>.tpl" and recursed.
static enumError extract_hst_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".hst"))
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
		if (sz < 0x40 || sz > 0x20000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz)
			goto done;
		if (memcmp (d, "HST\0", 4) || be32 (d + 4) != 1 || be32 (d + 12) != 0x0020af30)
			goto done;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s.tpl", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT HST:%s -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, path);
		err = ERR_OK;
		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (d + 12, 1, sz - 12, F.f) != (size_t)sz - 12)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %ld bytes failed: %s\n", sz - 12, path);
			ResetFile (&F, opt_preserve);
			if (!err)
				extract_one_file (path, 0, depth + 1);
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}
