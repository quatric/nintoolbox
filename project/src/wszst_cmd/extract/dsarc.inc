// Nippon Ichi "DSARC" archives (Disgaea DS and other NIS DS titles; little endian). "DSARC FL": u32 count, u32 1, then count entries {char name[40], u32 size, u32 offset}
// from 0x10. "DSARCIDX": u32 count, u32 0, count u16 global ids, padding to 4, then the same entries. Members (".imy" images, ".msnd" sound, ".dat" tables) are written as
// "<file>.d/<name>" (a repeated name gets "_<n>" before the extension unless offset and size repeat too).
static enumError extract_dsarc_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	const bool fl = file_has_magic (arg, "DSARC FL", 8);
	if (!fl && !file_has_magic (arg, "DSARCIDX", 8))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	const size_t n = sz >= 16 ? rd_le32 (raw + 8) : 0, tab = fl ? 16 : (16 + 2 * n + 3) & ~(size_t)3;
	if (!n || n > 100000 || tab + 48 * n > sz)
		goto done;
	for (size_t i = 0; i < n; i++)
	{
		const u8 *e = raw + tab + 48 * i;
		char nm[41];
		memcpy (nm, e, 40), nm[40] = 0;
		if (rd_le32 (e + 44) > sz || rd_le32 (e + 40) > sz - rd_le32 (e + 44) || !valid_sarc_path (nm))
			goto done;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT DSARC:%s (%zu members) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
	err = ERR_OK;
	for (size_t i = 0; i < n && !err && !testmode; i++)
	{
		const u8 *e = raw + tab + 48 * i;
		const size_t s = rd_le32 (e + 40);
		uint dup = 0; // names repeat (bg.dat, voice.dat): skip exact repeats, number the rest
		bool same = false;
		for (size_t k = 0; k < i && !same; k++)
			if (!memcmp (raw + tab + 48 * k, e, 40))
				same = !memcmp (raw + tab + 48 * k + 40, e + 40, 8), dup++;
		if (same)
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%.40s", dest, basedir ? basedir : "", (const char *)e);
		if (dup)
		{
			char *dot = strrchr (path, '.'), ext[48] = "";
			if (dot && !strchr (dot, '/'))
				snprintf (ext, sizeof (ext), "%s", dot), *dot = 0;
			snprintf (path + strlen (path), sizeof (path) - strlen (path), "_%u%s", dup, ext);
		}
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (raw + rd_le32 (e + 44), 1, s, F.f) != s)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", s, path);
		ResetFile (&F, opt_preserve);
	}
done:
	FREE (raw);
	return err;
}
