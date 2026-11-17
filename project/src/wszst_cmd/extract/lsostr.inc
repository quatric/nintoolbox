// Zorro (The Destiny of Zorro, Wii) "GameData/data/Language/*.lso" string tables: a sequence of {u32 LE length, bytes} strings (no terminator). Written as "<file>.tsv" (index, file offset, text; \n, \t, \\ escaped).
static enumError extract_lsostr_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".lso"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	if (sz < 0x10)
		goto done;
	{
		c.d = raw;
		c.sz = sz;
		c.ok = true;
		pcr_emit (&c, "index\toffset\ttext\n");
		uint n = 0;
		size_t p = 0;
		while (p + 4 <= sz)
		{
			const u32 len = rd_le32 (raw + p);
			if (len > 0x10000 || p + 4 + len > sz)
			{
				n = 0;
				break;
			}
			pcr_emit (&c, "%u\t%u\t", n++, (uint)p + 4);
			for (size_t k = p + 4; k < p + 4 + len; k++)
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
			p += 4 + len;
		}
		if (!n)
			goto done;
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.tsv", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT LSO:%s -> TSV:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT LSO:%s (%u strings) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, n, dest);
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
