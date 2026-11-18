// Sega "*.gms" motion packs (Ghost Squad "motion/*.gms"): big-endian {u32 count, count * u32 member offset}; the first member starts right after the table, members run back to back and the last one ends at the end of the file. Members (NGIF/NGMO motion data) are written to "<file>.d/NNNN.bin".
static enumError extract_gms_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".gms"))
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
		const uint n = be32 (d);
		if (!n || n > 100000 || 4 + 4 * (size_t)n > (size_t)sz || be32 (d + 4) != 4 + 4 * n)
			goto done;
		for (uint i = 0; i < n; i++)
		{
			const u32 o = be32 (d + 4 + 4 * i), e = i + 1 < n ? be32 (d + 8 + 4 * i) : (u32)sz;
			if (e <= o || e > (u32)sz)
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT GMS:%s (%u members) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u32 o = be32 (d + 4 + 4 * i), e = i + 1 < n ? be32 (d + 8 + 4 * i) : (u32)sz;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%04u.bin", dest, i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (d + o, 1, e - o, F.f) != e - o)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e - o, path);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}
