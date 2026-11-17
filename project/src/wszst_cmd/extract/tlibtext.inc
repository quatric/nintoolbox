// Cabela's (Activision/Fun Labs, Wii) "Language/TextLib.<lang>.bkp" text library ("TLIBv1.0d": little-endian header {u32 pool size, u32 string count, u32 table offset, ...}, id/offset tables, then a pool of
// NUL-terminated UTF-16LE strings, each optionally preceded by a 16-bit id). The tables are not interpreted: every 2-byte aligned run of Latin text that ends with a NUL is listed. Written as "<file>.tsv"
// (file offset, text; \n, \t, \\ escaped).
static enumError extract_tlibtext_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bkp"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	if (sz < 0x400 || memcmp (raw, "TLIBv1.0", 8))
		goto done;
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "offset\ttext\n");
	uint n = 0;
	for (size_t p = 0x320; p + 2 <= sz;)
	{
		size_t q = p;
		uint letters = 0, len = 0;
		while (q + 2 <= sz)
		{
			const uint u = rd_le16 (raw + q);
			if (!u || !((u >= 0x20 && u < 0x7f) || (u >= 0xa0 && u < 0x250) || u == '\n' || u == '\r' || u == '\t'))
				break;
			if ((u | 0x20) >= 'a' && (u | 0x20) <= 'z')
				letters++;
			len++;
			q += 2;
		}
		if (len >= 3 && q + 2 <= sz && !rd_le16 (raw + q) && letters * 2 >= len)
		{
			pcr_emit (&c, "%u\t", (uint)p);
			for (size_t k = p; k < q; k += 2)
			{
				const uint u = rd_le16 (raw + k);
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
				else
					pcr_emit (&c, "%c%c", 0xc0 | u >> 6, 0x80 | (u & 0x3f));
			}
			pcr_emit (&c, "\n");
			n++;
			p = q + 2;
		}
		else
			p += 2;
	}
	if (n && c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.tsv", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT TLIB:%s -> TSV:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT TLIB:%s (%u strings) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, n, dest);
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
