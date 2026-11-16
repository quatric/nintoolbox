// Wii Chess (Europe) "MsgTbl/Mes<LANG>.mbb" message tables, big-endian "TBB2" container: {"TBB2", u32 0x10, u32 section count, u32 total size, u32 first section offset 0x20, ...}; the "MTBL" section at
// 0x20 holds {"MTBL", u32 0x10, u32 section size, u32 string count, count u32 offsets relative to 0x30 (0 = no string)}, followed by NUL-terminated UTF-16BE strings. Written as "<file>.tsv" (index, text; \n, \t,
// \\ escaped). Only the MTBL section is decoded.
static enumError extract_mbbtbl_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".mbb"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	const u32 n = sz >= 0x30 ? rd_be32 (raw + 0x2c) : 0;
	if (sz < 0x30 || memcmp (raw, "TBB2", 4) || memcmp (raw + 0x20, "MTBL", 4) || !n || n > 100000 || 0x30 + 4 * (u64)n > sz)
		goto done;
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "index\ttext\n");
	for (u32 i = 0; i < n; i++)
	{
		const u32 o = rd_be32 (raw + 0x30 + 4 * (size_t)i);
		if (o && (0x30 + (u64)o + 2 > sz || o & 1))
			goto done;
		pcr_emit (&c, "%u\t", i);
		for (size_t p = o ? 0x30 + (size_t)o : sz; p + 2 <= sz; p += 2)
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
			fprintf (stdlog, "WOULD EXTRACT MBB:%s -> TSV:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT MBB:%s (%u strings) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, n, dest);
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
