// "SSAM" archives (Square Enix DS: Final Fantasy III / IV, about 15 titles; little endian): "SSAM", u32 count, then count entries {u32 offset, u32 size, char name[32]}; the
// offsets count from the end of that table. Members are Nitro files (NCGR, NCLR, NANR, ...), "*.lz" ones LZ10 compressed; written as "<file>.d/<name>" unchanged.
static enumError extract_ssam_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!file_has_magic (arg, "SSAM", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	const size_t n = sz >= 8 ? rd_le32 (raw + 4) : 0, base = 8 + 40 * n;
	if (!n || n > 100000 || base > sz)
		goto done;
	for (size_t i = 0; i < n; i++)
	{
		const u8 *e = raw + 8 + 40 * i;
		char nm[33];
		memcpy (nm, e + 8, 32), nm[32] = 0;
		if (rd_le32 (e) > sz - base || rd_le32 (e + 4) > sz - base - rd_le32 (e) || !valid_sarc_path (nm))
			goto done;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SSAM:%s (%zu members) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
	err = ERR_OK;
	for (size_t i = 0; i < n && !err && !testmode; i++)
	{
		const u8 *e = raw + 8 + 40 * i;
		const size_t s = rd_le32 (e + 4);
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%.32s", dest, basedir ? basedir : "", (const char *)e + 8);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (raw + base + rd_le32 (e), 1, s, F.f) != s)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", s, path);
		ResetFile (&F, opt_preserve);
	}
done:
	FREE (raw);
	return err;
}
