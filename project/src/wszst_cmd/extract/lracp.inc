// Line Rider Freestyle "*.acp" animated-character packs: big-endian {u32 count, ...settings...} with a table of count + 1 u32 offsets ending exactly where the first "LRMH" chunk starts; the last entry is the file size. Chunks (LRMH models) are written to "<file>.d/NN.lrmh".
static enumError extract_lracp_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".acp"))
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
		if (sz < 0x60 || sz > 0x10000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz)
			goto done;
		const uint n = be32 (d);
		if (!n || n > 256)
			goto done;
		size_t first = 0;
		for (size_t i = 4; i + 4 <= (size_t)sz && i < 0x200; i++)
			if (!memcmp (d + i, "LRMH", 4))
			{
				first = i;
				break;
			}
		if (first < 4 * (n + 1) + 4)
			goto done;
		const u8 *tab = d + first - 4 * (n + 1);
		if (be32 (tab) != first || be32 (tab + 4 * n) != (u32)sz)
			goto done;
		for (uint i = 0; i < n; i++)
			if (be32 (tab + 4 * i) >= be32 (tab + 4 * i + 4) || memcmp (d + be32 (tab + 4 * i), "LRMH", 4))
				goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT LRACP:%s (%u chunks) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u32 a = be32 (tab + 4 * i), b = be32 (tab + 4 * i + 4);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%02u.lrmh", dest, i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (d + a, 1, b - a, F.f) != b - a)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", b - a, path);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}
