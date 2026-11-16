// Scrabble Interactive (Wii, Europe: AI/RackLeaves/<dictionary>/AscSortedSyn<N>.bin) rack-leave value tables, big-endian:
//   count * {char key[N] (letters 'a'.., '`' is the blank, sorted ascending), u16 value}, N = 1..6 from the file name,
//   no header (the sibling ElementsCount.bin holds the record counts of tables 1..6 as little-endian u32s after a total).
// The values cluster around 0x8000 (a biased fixed-point equity). Written as "<file>.tsv" with columns key, value, value-0x8000.
static enumError extract_ascsyn_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	if (strncmp (base, "AscSortedSyn", 12) || base[12] < '1' || base[12] > '6' || strcmp (base + 13, ".bin"))
		return ERR_NOTHING_TO_DO;
	const uint klen = (uint)(base[12] - '0');
	const size_t rec = klen + 2;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	bool good = sz && sz % rec == 0;
	const size_t n = good ? sz / rec : 0;
	for (size_t i = 0; good && i < n; i++)
	{
		const u8 *r = raw + i * rec;
		for (uint k = 0; k < klen; k++)
			if (r[k] < 0x60 || r[k] > 0x7a)
				good = false;
		if (good && i && memcmp (r - rec, r, klen) >= 0)
			good = false;
	}
	if (!good)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "key\tvalue\tsigned\n");
	for (size_t i = 0; i < n; i++)
	{
		const u8 *r = raw + i * rec;
		const uint v = ((uint)r[klen] << 8) | r[klen + 1];
		pcr_emit (&c, "%.*s\t%u\t%d\n", (int)klen, (const char *)r, v, (int)v - 0x8000);
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.tsv", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT ASCSYN:%s -> TSV:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT ASCSYN:%s -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
