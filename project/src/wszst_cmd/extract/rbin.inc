// Dragon Ball - Tenkaichi Daibouken "*.RBIN": be32 section count n, 12 zero bytes, then n+1 ascending be32 offsets from 0x10 (section starts, the last one is the file size). Sections are written to "<file>.d/NN.bin".
static enumError extract_rbin_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".rbin"))
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
		if (sz < 0x40 || sz > 0x40000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz)
			goto done;
		const u32 n = be32 (d);
		if (!n || n > 64 || be32 (d + 4) || be32 (d + 8) || be32 (d + 12))
			goto done;
		u32 off[65];
		for (uint i = 0; i <= n; i++)
		{
			off[i] = be32 (d + 0x10 + 4 * i);
			if (i && off[i] < off[i - 1])
				goto done;
		}
		if (off[0] < 0x10 + 4 * (n + 1) || off[n] != (u32)sz)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT RBIN:%s (%u sections) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u32 s = off[i + 1] - off[i];
			if (!s)
				continue;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%02u.bin", dest, i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (d + off[i], 1, s, F.f) != s)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
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
