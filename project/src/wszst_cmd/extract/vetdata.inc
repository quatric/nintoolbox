// Real Stories: Veterinarian (Wii, France) data tables.
//  ".edf" animation event lists, little-endian: u32 0, u32 count, count * {u8 length, name} (animation file names), then {u8 length, NAME, u32 value} records until the end. Written as "<file>.txt".
//  "dic_*.dic" word tables: a flat array of NUL-padded 0x23-byte Windows-1252 text fields (ages, units, words, phrases; the grouping into records is not known). Written as "<file>.tsv" (index, text).
static void vet_emit_cp1252 (pcr_ctx_t *c, const u8 *s, size_t n)
{
	for (size_t i = 0; i < n && s[i]; i++)
	{
		const uint u = s[i];
		if (u == '\t' || u == '\n' || u == '\r')
			pcr_emit (c, " ");
		else if (u < 0x80)
			pcr_emit (c, "%c", (int)u);
		else if (u >= 0xa0)
			pcr_emit (c, "%c%c", 0xc0 | u >> 6, 0x80 | (u & 0x3f));
		else
			pcr_emit (c, "?");
	}
}

static enumError vet_write_text (ccp arg, ccp suffix, ccp what, pcr_ctx_t *c, uint n)
{
	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s%s", arg, suffix);
	if (testmode)
	{
		fprintf (stdlog, "WOULD EXTRACT %s:%s -> %s\n", what, arg, dest);
		return ERR_OK;
	}
	File_t F;
	enumError err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f && fwrite (c->out, 1, c->len, F.f) != c->len)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)c->len, dest);
	ResetFile (&F, opt_preserve);
	if (verbose >= 0 && !err)
		fprintf (stdlog, "%sEXTRACT %s:%s (%u entries) -> %s\n", verbose > 0 ? "\n" : "", what, arg, n, dest);
	return err;
}

static enumError extract_vetdata_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	const bool edf = is_ext (arg, ".edf"), dic = is_ext (arg, ".dic");
	if (!edf && !dic)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	if (edf)
	{
		const u32 n = sz >= 8 ? rd_le32 (raw + 4) : 0;
		if (sz < 8 || rd_le32 (raw) || !n || n > 1000)
			goto done;
		size_t p = 8;
		pcr_emit (&c, "# files\n");
		for (u32 i = 0; i < n; i++)
		{
			if (p >= sz || p + 1 + raw[p] > sz || !raw[p])
				goto done;
			for (uint k = 0; k < raw[p]; k++)
				if (raw[p + 1 + k] < 0x20 || raw[p + 1 + k] > 0x7e)
					goto done;
			pcr_emit (&c, "%.*s\n", (int)raw[p], raw + p + 1);
			p += 1 + raw[p];
		}
		pcr_emit (&c, "# events\n");
		while (p < sz)
		{
			if (!raw[p] || p + 5 + raw[p] > sz)
				goto done;
			pcr_emit (&c, "%.*s\t%u\n", (int)raw[p], raw + p + 1, rd_le32 (raw + p + 1 + raw[p]));
			p += 5 + raw[p];
		}
		if (c.ok)
			err = vet_write_text (arg, ".txt", "EDF", &c, n);
	}
	else
	{
		if (sz < 0x23 || sz % 0x23)
			goto done;
		const size_t n = sz / 0x23;
		pcr_emit (&c, "index\ttext\n");
		for (size_t i = 0; i < n; i++)
		{
			const u8 *s = raw + i * 0x23;
			if (s[0x22])
				goto done;
			pcr_emit (&c, "%u\t", (uint)i);
			vet_emit_cp1252 (&c, s, 0x23);
			pcr_emit (&c, "\n");
		}
		if (c.ok)
			err = vet_write_text (arg, ".tsv", "DIC", &c, (uint)n);
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
