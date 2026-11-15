// "BGDT" stage background tables (Teolsil Kirby Iyagi / Kirby's Epic Yarn Korea, Wii: stage/stage*/bg.dat), big-endian:
//   {"BGDT", u32 1, u32 0, u32 capacity (16), u32 record count}, count * 52 byte records (13 words, ints and floats);
//   file size == 20 + 52 * count in all 51 retail files (72 bytes = 1 record, 852 bytes = 16 records). The field
//   meanings are not known; written as "<file>.json" with the words unnamed (floats where the value looks like one).
static enumError extract_bgdt_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".dat"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 n = sz >= 20 ? rd_be32 (raw + 16) : 0;
	if (sz < 20 || memcmp (raw, "BGDT", 4) || rd_be32 (raw + 4) != 1 || n > 0x10000 || sz != 20 + (size_t)n * 52)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "{\n  \"format\": \"BGDT\",\n  \"capacity\": %u,\n  \"records\": [", rd_be32 (raw + 12));
	for (u32 i = 0; i < n; i++)
	{
		pcr_emit (&c, "%s\n    [", i ? "," : "");
		baao_words (&c, raw + 20 + (size_t)i * 52, 52);
		pcr_emit (&c, "]");
	}
	pcr_emit (&c, "\n  ]\n}\n");
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT BGDT:%s -> JSON:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT BGDT:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
#pragma pop_macro("free")
#pragma pop_macro("calloc")
