// Necro-Nesia (Wii) "<LANG>/*.bmsg" message tables, little-endian: {u32 1, u32 string section offset, u32 0, u32 pair count}, 8-byte {u16 label index, u16 string index, u16 1, u16 0} records from 0x10, then at the
// section offset {u32 1, u32 string count, count u32 offsets relative to the section start} and NUL-terminated UTF-16LE strings (control codes such as 0x001b are kept as "\u001b"). Written as "<file>.tsv"
// (index, text; \n, \t, \\ escaped). The label table is not decoded.
static enumError extract_bmsgtext_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bmsg"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	const u32 so = sz >= 0x20 ? rd_le32 (raw + 4) : 0;
	if (sz < 0x20 || rd_le32 (raw) != 1 || so < 0x10 || so + 8 > sz || rd_le32 (raw + so) != 1)
		goto done;
	{
		const u32 n = rd_le32 (raw + so + 4);
		if (!n || n > 100000 || so + 8 + 4 * (u64)n > sz)
			goto done;
		for (u32 i = 0; i < n; i++)
		{
			const u64 o = (u64)so + rd_le32 (raw + so + 8 + 4 * (size_t)i);
			if (o + 2 > sz || (o & 1))
				goto done;
		}
		c.d = raw;
		c.sz = sz;
		c.ok = true;
		pcr_emit (&c, "index\ttext\n");
		for (u32 i = 0; i < n; i++)
		{
			pcr_emit (&c, "%u\t", i);
			for (size_t p = (size_t)so + rd_le32 (raw + so + 8 + 4 * (size_t)i); p + 2 <= sz; p += 2)
			{
				uint u = rd_le16 (raw + p);
				if (!u)
					break;
				if (u >= 0xd800 && u < 0xdc00 && p + 4 <= sz && rd_le16 (raw + p + 2) >= 0xdc00 && rd_le16 (raw + p + 2) < 0xe000)
				{
					u = 0x10000 + ((u - 0xd800) << 10) + (rd_le16 (raw + p + 2) - 0xdc00);
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
				else if (u < 0x20)
					pcr_emit (&c, "\\u%04x", u);
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
				fprintf (stdlog, "WOULD EXTRACT BMSG:%s -> TSV:%s\n", arg, dest);
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
					fprintf (stdlog, "%sEXTRACT BMSG:%s (%u strings) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, n, dest);
			}
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
