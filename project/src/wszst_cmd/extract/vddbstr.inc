// Dancing Dots "VDDB" string databases (My Baby Boy and Girl 2 etc., Database/LocalString/<LANG>.vddb), big-endian: {0xdddb, u16 6, u32 0x7d, name "LocalString"...}, u32 string count at 0x34, u32 pool offset at 0x40
// (the header holds more relocation fields, not decoded). The pool is a run of {key: NUL-terminated ASCII padded to 4 bytes, text: UTF-16BE ending in a 0x0000 terminator, padded to 4 bytes; an empty text is omitted entirely} pairs, read until
// the count is reached, the data ends or an empty key appears. Written as "<file>.tsv" (key, text; \n, \t, \\ escaped).
static enumError extract_vddbstr_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".vddb"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	const u32 n = sz >= 0x48 ? rd_be32 (raw + 0x34) : 0, po = sz >= 0x48 ? rd_be32 (raw + 0x40) : 0;
	if (sz < 0x48 || raw[0] != 0xdd || raw[1] != 0xdb || !n || n > 100000 || po < 0x48 || po >= sz)
		goto done;
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "key\ttext\n");
	{
		size_t p = po;
		uint cnt = 0;
		while (p < sz && cnt < n)
		{
			const void *z = memchr (raw + p, 0, sz - p);
			if (!z)
				break;
			const size_t kl = (const u8 *)z - (raw + p);
			if (!kl)
				break;
			bool ok = true;
			for (size_t k = 0; k < kl; k++)
				if (raw[p + k] < 0x20 || raw[p + k] > 0x7e)
					ok = false;
			if (!ok)
				goto done;
			pcr_emit (&c, "%.*s\t", (int)kl, raw + p);
			p = (p + kl + 1 + 3) & ~(size_t)3;
			// an empty text is stored as nothing at all: the next key follows directly
			const bool nokey = p + 2 <= sz && raw[p] >= 0x20 && raw[p] < 0x7f && raw[p + 1] >= 0x20 && raw[p + 1] < 0x7f;
			while (!nokey && p + 2 <= sz && (raw[p] || raw[p + 1]))
			{
				uint u = rd_be16 (raw + p);
				p += 2;
				if (u >= 0xd800 && u < 0xdc00 && p + 2 <= sz && rd_be16 (raw + p) >= 0xdc00 && rd_be16 (raw + p) < 0xe000)
				{
					u = 0x10000 + ((u - 0xd800) << 10) + (rd_be16 (raw + p) - 0xdc00);
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
			if (!nokey)
				p = (p + 2 + 3) & ~(size_t)3;
			pcr_emit (&c, "\n");
			cnt++;
		}
		if (cnt < 10)
			goto done;
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.tsv", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT VDDB:%s -> TSV:%s\n", arg, dest);
			err = ERR_OK;
		}
		else if (c.ok)
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (c.out, 1, c.len, F.f) != c.len)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)c.len, dest);
			ResetFile (&F, opt_preserve);
			if (verbose >= 0 && !err)
				fprintf (stdlog, "%sEXTRACT VDDB:%s (%u strings) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, cnt, dest);
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
