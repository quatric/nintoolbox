// Disney's Chicken Little: Ace in Action (Wii) "data/lang/*.dct" string dictionaries, little-endian: "DICT", u32 0x1000, u32 checksum, u32 7, u32 count, count * {u32 string hash, u32 offset}
// where the NUL terminated UTF-8 text starts at (address of the offset field + offset + 1), i.e. 0x19 + 8 * n + offset, behind the table. Written as "<file>.tsv" (hash, text; tab,
// newline and backslash escaped).
static enumError extract_dctdict_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".dct"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x30 || memcmp (raw, "DICT", 4) || rd_le32 (raw + 4) != 0x1000)
		goto done;
	{
		const u32 n = rd_le32 (raw + 16);
		if (!n || n > 1000000 || 0x14 + 8 * (u64)n > sz)
			goto done;
		for (u32 i = 0; i < n; i++)
		{
			const u64 s = 0x19 + 8 * (u64)i + rd_le32 (raw + 0x14 + 8 * (size_t)i + 4);
			if (s >= sz || !memchr (raw + s, 0, sz - s))
				goto done;
		}
		pcr_emit (&c, "hash\ttext\n");
		for (u32 i = 0; i < n; i++)
		{
			const u8 *t = raw + 0x19 + 8 * (size_t)i + rd_le32 (raw + 0x14 + 8 * (size_t)i + 4);
			pcr_emit (&c, "%08x\t", rd_le32 (raw + 0x14 + 8 * (size_t)i));
			for (; *t; t++)
			{
				if (*t == '\t')
					pcr_emit (&c, "\\t");
				else if (*t == '\n')
					pcr_emit (&c, "\\n");
				else if (*t == '\r')
					pcr_emit (&c, "\\r");
				else if (*t == '\\')
					pcr_emit (&c, "\\\\");
				else
					pcr_emit (&c, "%c", *t);
			}
			pcr_emit (&c, "\n");
		}
		if (c.ok)
		{
			char dest[PATH_MAX];
			snprintf (dest, sizeof (dest), "%s.tsv", arg);
			if (testmode)
			{
				fprintf (stdlog, "WOULD EXTRACT DICT:%s -> TSV:%s\n", arg, dest);
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
					fprintf (stdlog, "%sEXTRACT DICT:%s (%u strings) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, n, dest);
			}
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
