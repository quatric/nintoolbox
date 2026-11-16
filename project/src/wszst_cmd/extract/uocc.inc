// Samba de Amigo (Wii) challenge tables ("challenge*.bin", "challenge/challenge_*.bin"), big-endian: a run of blocks
//   "UOCC", u32 block size (0x58), u16 item count (5), u16 block index, then count * 16-byte items {u8 0, "IVD", u8 a, u8 b, u8 c, u8 d, u32 x, u32 y, u8 pad}
// (the first item's leading zero is the last header byte), and a closing "DNEC". Block = 12 header bytes + count * 16 - 4, i.e. 0x58 with 5 items.
// The meaning of the item fields is not documented; they are written verbatim to "<file>.tsv" (columns block, item, a, b, c, d, x, y).
static enumError extract_uocc_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	bool good = sz > 16 && !memcmp (raw, "UOCC", 4) && !memcmp (raw + sz - 4, "DNEC", 4);
	size_t p = 0;
	uint blocks = 0;
	while (good && p + 4 < sz)
	{
		if (p + 12 > sz || memcmp (raw + p, "UOCC", 4))
		{
			good = false;
			break;
		}
		const u32 bs = rd_be32 (raw + p + 4), n = raw[p + 8] << 8 | raw[p + 9];
		if (!n || n > 64 || bs != 12 + 16 * (u32)n - 4 || p + 8 + bs > sz)
			good = false;
		for (uint i = 0; good && i < n; i++)
			if (memcmp (raw + p + 13 + 16 * i, "IVD", 3))
				good = false;
		p += 8 + bs;
		blocks++;
	}
	if (!good || p + 4 != sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "block\titem\ta\tb\tc\td\tx\ty\n");
	p = 0;
	for (uint b = 0; b < blocks; b++)
	{
		const uint n = raw[p + 8] << 8 | raw[p + 9];
		for (uint i = 0; i < n; i++)
		{
			const u8 *r = raw + p + 16 + 16 * i;
			pcr_emit (&c, "%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\n", raw[p + 10] << 8 | raw[p + 11], i, r[0], r[1], r[2], r[3], rd_be32 (r + 4), rd_be32 (r + 8));
		}
		p += 8 + rd_be32 (raw + p + 4);
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.tsv", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT UOCC:%s -> TSV:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT UOCC:%s -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
