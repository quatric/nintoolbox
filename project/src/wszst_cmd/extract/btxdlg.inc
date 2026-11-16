// K11 - Kommissare im Einsatz (Germany) dialog tables "wii/Text/<LANG>/*.btx", big-endian: {u32 entry count, then per entry: u32 entry id, u32 field count, per field: u32 field kind, u32 UTF-16 unit count
// (including the terminating NUL), UTF-16BE text}. Kind 1 is the speaker, 2 the spoken line, 3 an emotion tag. Written as "<file>.tsv" (entry, kind, text; \n, \t, \\ escaped). The parse must consume the
// whole file exactly, otherwise the file is left alone.
static enumError extract_btxdlg_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".btx"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	const u32 n = sz >= 12 ? rd_be32 (raw) : 0;
	if (sz < 12 || !n || n > 100000)
		goto done;
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "entry\tkind\ttext\n");
	size_t p = 4;
	for (u32 i = 0; i < n; i++)
	{
		if (p + 8 > sz)
			goto bad;
		const u32 id = rd_be32 (raw + p), nf = rd_be32 (raw + p + 4);
		p += 8;
		if (nf > 64)
			goto bad;
		for (u32 f = 0; f < nf; f++)
		{
			if (p + 8 > sz)
				goto bad;
			const u32 kind = rd_be32 (raw + p), len = rd_be32 (raw + p + 4);
			p += 8;
			if (len > 100000 || p + 2 * (size_t)len > sz)
				goto bad;
			pcr_emit (&c, "%u\t%u\t", id, kind);
			for (u32 k = 0; k < len; k++)
			{
				uint u = rd_be16 (raw + p + 2 * (size_t)k);
				if (!u)
					break;
				if (u == '\n')
					pcr_emit (&c, "\\n");
				else if (u == '\t')
					pcr_emit (&c, "\\t");
				else if (u == '\\')
					pcr_emit (&c, "\\\\");
				else if (u == '\r')
					;
				else if (u < 0x80)
					pcr_emit (&c, "%c", (int)u);
				else if (u < 0x800)
					pcr_emit (&c, "%c%c", 0xc0 | u >> 6, 0x80 | (u & 0x3f));
				else
					pcr_emit (&c, "%c%c%c", 0xe0 | u >> 12, 0x80 | ((u >> 6) & 0x3f), 0x80 | (u & 0x3f));
			}
			pcr_emit (&c, "\n");
			p += 2 * (size_t)len;
		}
	}
	if (p != sz)
		goto bad;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.tsv", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT BTX:%s -> TSV:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT BTX:%s (%u entries) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, n, dest);
		}
	}
	goto done;
bad:
	err = ERR_NOTHING_TO_DO;
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
