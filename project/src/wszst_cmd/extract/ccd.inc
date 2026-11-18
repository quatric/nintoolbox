// Tom Clancy's H.A.W.X. 2 "*.ccd" object containers: be32 size, be32 record count n, be32 0x1c, 16 more zero bytes... n ascending be32 record offsets from +0x1c (the first record starts right after the table), each record begins with a be32 pointer to its NUL terminated name in the string pool behind the last record. A record ends where the next starts, the last one at the lowest name pointer. Records are written to "<file>.d/NNNN_<name>.bin".
static enumError extract_ccd_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".ccd"))
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
		const u32 n = be32 (d + 4);
		if (!n || n > 100000 || be32 (d + 8) != 0x1c || 0x1c + 4 * (size_t)n > (size_t)sz)
			goto done;
		u32 prev = 0x1c + 4 * n, minptr = (u32)sz;
		for (uint i = 0; i < n; i++)
		{
			const u32 o = be32 (d + 0x1c + 4 * i);
			if (o != prev && (i == 0 || o <= prev))
				goto done;
			if (o < prev || o + 4 > (u32)sz)
				goto done;
			const u32 ptr = be32 (d + o);
			if (ptr >= (u32)sz)
				goto done;
			if (ptr < minptr)
				minptr = ptr;
			prev = o;
		}
		if (prev >= minptr)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT CCD:%s (%u records) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u32 o = be32 (d + 0x1c + 4 * i);
			const u32 e = i + 1 < n ? be32 (d + 0x1c + 4 * (i + 1)) : minptr;
			const u32 ptr = be32 (d + o);
			char nm[80];
			uint k = 0;
			for (u32 q = ptr; q < (u32)sz && d[q] && k < sizeof (nm) - 1; q++)
				nm[k++] = (d[q] < 0x20 || d[q] == '/' || d[q] == '\\') ? '_' : d[q];
			nm[k] = 0;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%04u_%s.bin", dest, i, nm);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && e > o && fwrite (d + o, 1, e - o, F.f) != e - o)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e - o, path);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}
