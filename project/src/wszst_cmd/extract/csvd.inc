// "CSVD" spawn table (Link's Crossbow Training, Wii: res/ItemTable/enemy_table.bin), big-endian:
//   {"CSVD", u32 record count, count * {u8 group id, u8 value, char name[8] (NUL padded)}} then 0xff padding.
// "#" names are empty slots. Written as "<file>.json".
static enumError extract_csvd_file (ccp arg, ccp basedir, uint depth)
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
	const u32 n = sz >= 8 ? rd_be32 (raw + 4) : 0;
	bool good = sz >= 8 && !memcmp (raw, "CSVD", 4) && n && n < 0x10000 && 8 + (size_t)n * 10 <= sz;
	for (size_t i = good ? 8 + (size_t)n * 10 : 0; good && i < sz; i++)
		if (raw[i] != 0xff)
			good = false;
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
	pcr_emit (&c, "{\n  \"format\": \"CSVD\",\n  \"records\": [");
	for (u32 i = 0; i < n; i++)
	{
		const u8 *r = raw + 8 + (size_t)i * 10;
		char nm[9];
		for (uint k = 0; k < 8; k++)
			nm[k] = r[2 + k] >= 0x20 && r[2 + k] < 0x7f && r[2 + k] != '"' && r[2 + k] != '\\' ? (char)r[2 + k] : (r[2 + k] ? '?' : 0);
		nm[8] = 0;
		pcr_emit (&c, "%s\n    {\"id\": %u, \"value\": %u, \"name\": \"%s\"}", i ? "," : "", r[0], r[1], nm);
	}
	pcr_emit (&c, "\n  ]\n}\n");
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT CSVD:%s -> JSON:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT CSVD:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
