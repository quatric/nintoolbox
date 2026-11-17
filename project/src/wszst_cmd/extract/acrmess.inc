// Another Code R (Wii) "REVOLUTION/**/MESS/*.MESS" text records, big-endian: {u32 count, count * u32 record offset}, each record {u32 id, u32 a, u32 b, u32 c, u32 text length, text (UTF-8, not NUL terminated)}
// ending where the next record starts (the last one at the end of the file). Written as "<file>.tsv" (index, id, a, b, c, text; \n, \t, \\ escaped).
static enumError extract_acrmess_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".mess"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	const u32 n = sz >= 8 ? rd_be32 (raw) : 0;
	if (sz < 0x30 || !n || n > 100000 || 4 + 4 * (u64)n > sz || rd_be32 (raw + 4) != 4 + 4 * n)
		goto done;
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "index\tid\ta\tb\tc\ttext\n");
	for (u32 i = 0; i < n; i++)
	{
		const u64 o = rd_be32 (raw + 4 + 4 * (size_t)i), e = i + 1 < n ? rd_be32 (raw + 8 + 4 * (size_t)i) : sz;
		if (o + 20 > e || e > sz)
			goto done;
		const u32 len = rd_be32 (raw + o + 16);
		if (o + 20 + len > e)
			goto done;
		pcr_emit (&c, "%u\t%u\t%u\t%u\t%u\t", i, rd_be32 (raw + o), rd_be32 (raw + o + 4), rd_be32 (raw + o + 8), rd_be32 (raw + o + 12));
		for (u32 k = 0; k < len; k++)
		{
			const u8 ch = raw[o + 20 + k];
			if (ch == '\n')
				pcr_emit (&c, "\\n");
			else if (ch == '\t')
				pcr_emit (&c, "\\t");
			else if (ch == '\\')
				pcr_emit (&c, "\\\\");
			else if (ch == '\r' || !ch)
				;
			else
				pcr_emit (&c, "%c", ch);
		}
		pcr_emit (&c, "\n");
	}
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.tsv", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT MESS:%s -> TSV:%s\n", arg, dest);
			err = ERR_OK;
		}
		else
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (c.out, 1, c.len, F.f) != c.len)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)c.len, dest);
			ResetFile (&F, opt_preserve);
			if (verbose >= 0 && !err)
				fprintf (stdlog, "%sEXTRACT MESS:%s (%u records) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, n, dest);
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
