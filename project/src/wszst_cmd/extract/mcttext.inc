// Storybook Workshop (USA) "MenuTextData_<lang>.mct" string table, big-endian: {u16 count, u16 count, ...0x24-byte header}, then count u32 offsets (in UTF-16 units) relative to the pool that follows the
// offset table (0x24 + 4*count), holding NUL-terminated UTF-16BE strings. Written as "<file>.tsv" (index, text; \n, \t, \\ escaped).
static enumError extract_mcttext_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".mct"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	const u32 n = sz >= 0x30 ? rd_be16 (raw) : 0;
	if (sz < 0x30 || rd_be16 (raw + 2) != n || rd_be32 (raw + 4) || !n || 0x24 + 4 * (u64)n > sz)
		goto done;
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "index\ttext\n");
	for (u32 i = 0; i < n; i++)
	{
		const size_t pool = 0x24 + 4 * (size_t)n;
		const u32 o = rd_be32 (raw + 0x24 + 4 * (size_t)i);
		if (pool + 2 * (u64)o + 2 > sz)
			goto done;
		pcr_emit (&c, "%u\t", i);
		for (size_t p = pool + 2 * (size_t)o; p + 2 <= sz; p += 2)
		{
			uint u = rd_be16 (raw + p);
			if (!u)
				break;
			if (u >= 0xd800 && u < 0xdc00 && p + 4 <= sz && rd_be16 (raw + p + 2) >= 0xdc00 && rd_be16 (raw + p + 2) < 0xe000)
			{
				u = 0x10000 + ((u - 0xd800) << 10) + (rd_be16 (raw + p + 2) - 0xdc00);
				p += 2;
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
			fprintf (stdlog, "WOULD EXTRACT MCT:%s -> TSV:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT MCT:%s (%u strings) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, n, dest);
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
