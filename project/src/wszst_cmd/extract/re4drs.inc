// Resident Evil 4 (Wii Edition) "*.drs" / "*.das": 0x20 byte title (Shift-JIS text), then 0x20 byte entries from +0x20 {be32 type, be32 size, be32 0, be32 offset, 16 zero bytes} terminated by type 0xffffffff; the first block starts at 0x400, blocks follow each other (up to 0xf bytes padding) and end at the end of the file. Blocks are written to "<file>.d/NN_<type>.bin"; type 4 blocks are nested containers of the same layout ("NN_4.drs", recursed).
static enumError extract_re4drs_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
		if (!is_ext (arg, ".drs") && !is_ext (arg, ".das"))
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
		if (sz < 0x420 || sz > 0x40000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz)
			goto done;
		uint n = 0;
		u64 next = 0x400;
		while (n < 30)
		{
			const u8 *e = d + 0x20 + 0x20 * n;
			if (be32 (e) == 0xffffffff)
				break;
			const u32 s = be32 (e + 4), o = be32 (e + 12);
			if (be32 (e + 8) || o < next || o > next + 0xf || (u64)o + s > (u64)sz)
				goto done;
			next = (u64)o + s;
			n++;
		}
		if (!n || n >= 30 || next != (u64)sz)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT RE4DRS:%s (%u blocks) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = d + 0x20 + 0x20 * i;
			const u32 t = be32 (e), s = be32 (e + 4), o = be32 (e + 12);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%02u_%u.%s", dest, i, t, t == 4 ? "drs" : "bin");
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && s && fwrite (d + o, 1, s, F.f) != s)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
			ResetFile (&F, opt_preserve);
			if (!err && t == 4)
				extract_one_file (path, 0, depth + 1);
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}
