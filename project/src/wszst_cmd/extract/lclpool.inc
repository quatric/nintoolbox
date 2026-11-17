// Disney Phineas and Ferb: Across the 2nd Dimension (Wii) "Global/<lang>.lcl" localisation tables, big-endian: {u32 record count, records of hashes and string offsets, ...} followed by a pool of NUL-terminated
// UTF-8 strings (keys, speaker names and texts). The records are not interpreted: the pool starting at the first string offset (the second word of the first record) is split at NULs. Written as "<file>.tsv"
// (index, file offset, text; \n, \t, \\ escaped).
static enumError extract_lclpool_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".lcl"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	const u32 cnt = sz >= 0x40 ? rd_be32 (raw) : 0, first = sz >= 0x40 ? rd_be32 (raw + 8) : 0;
	if (sz < 0x40 || !cnt || cnt > 100000 || first < 0x40 || first >= sz || 12 * (u64)cnt > first || raw[first] < 0x20)
		goto done;
	{
		c.d = raw;
		c.sz = sz;
		c.ok = true;
		pcr_emit (&c, "index\toffset\ttext\n");
		uint n = 0;
		size_t p = first;
		while (p < sz)
		{
			size_t q = p;
			while (q < sz && raw[q])
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
			fprintf (stdlog, "WOULD EXTRACT LCL:%s -> TSV:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT LCL:%s (%u strings) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, n, dest);
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
