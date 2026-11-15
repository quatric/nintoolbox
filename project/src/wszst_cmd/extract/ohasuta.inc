// Oha Suta Produce! Gentei Collabo Game Disc (Wii) script containers, big-endian, both written to "<file>.json":
//   "WCES0901" event: {tag(8), u32 record count, u32 0}, then count records of 8 u32 words (file size = 16 + 32*count)
//   "WCDR0900" script: {tag(8), u32 entry count, u32 header word}, then count entries of {u16 id, u16 value}
//                      (file size = 16 + 4*count)
// The meaning of the words (opcodes, arguments) is not known, so they are written unnamed.
static enumError extract_ohasuta_file (ccp arg, ccp basedir, uint depth)
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
	const bool es = sz >= 16 && !memcmp (raw, "WCES0901", 8), dr = sz >= 16 && !memcmp (raw, "WCDR0900", 8);
	const u32 n = sz >= 16 ? rd_be32 (raw + 8) : 0;
	if (!(es && sz == 16 + (size_t)32 * n) && !(dr && sz == 16 + (size_t)4 * n))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	if (es)
	{
		pcr_emit (&c, "{\n  \"format\": \"WCES0901\",\n  \"records\": [");
		for (u32 i = 0; i < n; i++)
		{
			pcr_emit (&c, "%s\n    [", i ? "," : "");
			for (uint k = 0; k < 8; k++)
				pcr_emit (&c, "%s%u", k ? ", " : "", rd_be32 (raw + 16 + i * 32 + k * 4));
			pcr_emit (&c, "]");
		}
	}
	else
	{
		pcr_emit (&c, "{\n  \"format\": \"WCDR0900\",\n  \"header\": %u,\n  \"entries\": [", rd_be32 (raw + 12));
		for (u32 i = 0; i < n; i++)
			pcr_emit (&c, "%s%s[%u, %u]", i ? "," : "", (i & 7) ? " " : "\n    ", rd_be16 (raw + 16 + i * 4),
				rd_be16 (raw + 18 + i * 4));
	}
	pcr_emit (&c, "\n  ]\n}\n");
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT OHASUTA:%s -> JSON:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT OHASUTA:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
