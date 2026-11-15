// "v1b" arrangement tables (Nights: Journey of Dreams, Wii: arrangement/*.bin), big-endian:
//   {"v1b\0", u32 record count, u32 trailer size}, count * 60 byte records (15 words: floats and ints), then a
//   trailer of "trailer size" bytes (u32 words); file size == 12 + 60 * count + trailer size in all 134 retail
//   files (27 more are empty 12 byte stubs). The field meanings are not known; the words are written unnamed
//   (floats where the value looks like one) to "<file>.json".
static enumError extract_v1b_file (ccp arg, ccp basedir, uint depth)
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
	const u32 n = sz >= 12 ? rd_be32 (raw + 4) : 0, tr = sz >= 12 ? rd_be32 (raw + 8) : 0;
	if (sz < 12 || memcmp (raw, "v1b", 4) || n > 0x100000 || (tr & 3) || sz != 12 + (size_t)n * 60 + tr)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "{\n  \"format\": \"v1b\",\n  \"records\": [");
	for (u32 i = 0; i < n; i++)
	{
		pcr_emit (&c, "%s\n    [", i ? "," : "");
		baao_words (&c, raw + 12 + (size_t)i * 60, 60);
		pcr_emit (&c, "]");
	}
	pcr_emit (&c, "\n  ],\n  \"trailer\": [");
	baao_words (&c, raw + 12 + (size_t)n * 60, tr);
	pcr_emit (&c, "]\n}\n");
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT V1B:%s -> JSON:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT V1B:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
#pragma pop_macro("free")
#pragma pop_macro("calloc")
