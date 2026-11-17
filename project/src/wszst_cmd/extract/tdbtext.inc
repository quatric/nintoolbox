// SEGA Super Monkey Ball (Banana Blitz, Step & Roll, Wii) "text/text_db_*.tdb" text tables, big-endian: {"TXDB", u32 3, u32 0, u32 end, u32 index offset, u32 ?, u32 pool offset, u32 pool size}, an index of
// {u16 offset, u16 length} pairs and a pool of NUL-terminated, 4-byte padded ASCII strings. The pool is split at NULs and written as "<file>.tsv" (index, file offset, text; \n, \t, \\ escaped).
static enumError extract_tdbtext_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".tdb"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	const u32 first = sz >= 0x40 ? rd_be32 (raw + 0x18) : 0, psz = sz >= 0x40 ? rd_be32 (raw + 0x1c) : 0;
	if (sz < 0x40 || memcmp (raw, "TXDB", 4) || first < 0x20 || first >= sz || psz > sz - first)
		goto done;
	{
		c.d = raw;
		c.sz = sz;
		c.ok = true;
		pcr_emit (&c, "index\toffset\ttext\n");
		uint n = 0;
		size_t p = first;
		while (p < first + psz)
		{
			size_t q = p;
			while (q < first + psz && raw[q])
				q++;
			if (q > p)
			{
				pcr_emit (&c, "%u\t%u\t", n++, (uint)p);
				for (size_t k = p; k < q; k++)
				{
					const u8 ch = raw[k];
					if (ch == '\n')
						pcr_emit (&c, "\\n");
					else if (ch == '\t')
						pcr_emit (&c, "\\t");
					else if (ch == '\\')
						pcr_emit (&c, "\\\\");
					else if (ch == '\r')
						;
					else
						pcr_emit (&c, "%c", ch);
				}
				pcr_emit (&c, "\n");
			}
			p = q + 1;
		}
		if (!n)
			goto done;
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.tsv", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT TDB:%s -> TSV:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT TDB:%s (%u strings) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, n, dest);
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
