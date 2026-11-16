// Top Model Academy (Wii, Europe) "interface/texts.ctx" localisation table, big-endian: u32 strings per language, u32 highest id + 1 (approximately), u32 language count, then per language
// {u32 name length, name (e.g. "ger", "eng"), u32 block offset from 0x22-style end of the language table}, followed by the language blocks back to back; every block holds <count>
// records {u32 id, u32 length in UTF-16 units, UTF-16BE text without terminator}. Written as "<file>.tsv" (language, id, text; \n, \t, \\ escaped).
static enumError extract_ctxtext_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".ctx"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	const u32 cnt = sz >= 16 ? rd_be32 (raw) : 0, nl = sz >= 16 ? rd_be32 (raw + 8) : 0;
	if (sz < 16 || !cnt || cnt > 100000 || !nl || nl > 32)
		goto done;
	{
		char lang[32][8];
		u32 loff[32];
		size_t p = 12;
		for (u32 i = 0; i < nl; i++)
		{
			if (p + 8 > sz)
				goto done;
			const u32 l = rd_be32 (raw + p);
			if (!l || l > 7 || p + 4 + l + 4 > sz)
				goto done;
			memcpy (lang[i], raw + p + 4, l);
			lang[i][l] = 0;
			for (u32 k = 0; k < l; k++)
				if (!isalnum (raw[p + 4 + k]))
					goto done;
			p += 4 + l;
			loff[i] = rd_be32 (raw + p);
			p += 4;
		}
		const size_t base = p;
		c.d = raw;
		c.sz = sz;
		c.ok = true;
		pcr_emit (&c, "lang\tid\ttext\n");
		for (u32 i = 0; i < nl; i++)
		{
			size_t q = base + loff[i];
			for (u32 r = 0; r < cnt; r++)
			{
				if (q + 8 > sz)
					goto done;
				const u32 id = rd_be32 (raw + q), l = rd_be32 (raw + q + 4);
				if (l > 0x10000 || q + 8 + 2 * (size_t)l > sz)
					goto done;
				pcr_emit (&c, "%s\t%u\t", lang[i], id);
				for (u32 k = 0; k < l; k++)
				{
					uint u = rd_be16 (raw + q + 8 + 2 * (size_t)k);
					if (u >= 0xd800 && u < 0xdc00 && k + 1 < l)
					{
						const uint lo = rd_be16 (raw + q + 8 + 2 * (size_t)k + 2);
						if (lo >= 0xdc00 && lo < 0xe000)
						{
							u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
							k++;
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
				q += 8 + 2 * (size_t)l;
			}
		}
		if (c.ok)
		{
			char dest[PATH_MAX];
			snprintf (dest, sizeof (dest), "%s.tsv", arg);
			if (testmode)
			{
				fprintf (stdlog, "WOULD EXTRACT CTX:%s -> TSV:%s\n", arg, dest);
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
					fprintf (stdlog, "%sEXTRACT CTX:%s (%u languages x %u strings) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, nl, cnt, dest);
			}
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
