// Groovin' Blocks (Wii, Gems engine) "Lang/<lang>/Strings/*.lang" string tables, big-endian: u32 entry size (8), u32 count, count u32 offsets of {u32 text offset, u32 string hash}
// records (the records follow the offset table, 8 bytes each), texts are NUL-terminated UTF-16BE. Written as "<file>.tsv" (index, hash, text; \n, \t, \\ escaped).
static enumError extract_gemslang_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".lang"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	const u32 n = sz >= 16 ? rd_be32 (raw + 4) : 0;
	if (sz < 16 || rd_be32 (raw) != 8 || !n || n > 100000 || 8 + 4 * (u64)n > sz)
		goto done;
	for (u32 i = 0; i < n; i++)
	{
		const u64 r = rd_be32 (raw + 8 + 4 * (size_t)i);
		if (r + 8 > sz || rd_be32 (raw + r) >= sz || rd_be32 (raw + r) & 1)
			goto done;
	}
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "index\thash\ttext\n");
	for (u32 i = 0; i < n; i++)
	{
		const u8 *r = raw + rd_be32 (raw + 8 + 4 * (size_t)i);
		size_t p = rd_be32 (r);
		pcr_emit (&c, "%u\t%08x\t", i, rd_be32 (r + 4));
		while (p + 2 <= sz)
		{
			uint u = rd_be16 (raw + p);
			p += 2;
			if (!u)
				break;
			if (u >= 0xd800 && u < 0xdc00 && p + 2 <= sz)
			{
				const uint lo = rd_be16 (raw + p);
				if (lo >= 0xdc00 && lo < 0xe000)
				{
					u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
					p += 2;
				}
			}
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
			else if (u < 0x10000)
				pcr_emit (&c, "%c%c%c", 0xe0 | u >> 12, 0x80 | ((u >> 6) & 0x3f), 0x80 | (u & 0x3f));
			else
				pcr_emit (&c, "%c%c%c%c", 0xf0 | u >> 18, 0x80 | ((u >> 12) & 0x3f), 0x80 | ((u >> 6) & 0x3f), 0x80 | (u & 0x3f));
		}
		pcr_emit (&c, "\n");
	}
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.tsv", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT GEMS-LANG:%s -> TSV:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT GEMS-LANG:%s (%u strings) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, n, dest);
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
