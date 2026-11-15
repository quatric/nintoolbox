// "ai00" AI parameter table (One Piece Unlimited Adventure / Cruise, Wii: ai/ace_prm.bin), big-endian:
//   {"ai00", u32 version (3), u32 ...} followed by u32 words (ints or floats). The record layout and the field names
//   are not known, so the words are written unnamed (floats where the value looks like one) to "<file>.json".
static enumError extract_ai00_file (ccp arg, ccp basedir, uint depth)
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
	if (sz < 0x20 || (sz & 3) || memcmp (raw, "ai00", 4) || rd_be32 (raw + 4) > 0xff)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "{\n  \"format\": \"ai00\",\n  \"version\": %u,\n  \"words\": [", rd_be32 (raw + 4));
	for (size_t i = 8; i + 4 <= sz; i += 4)
	{
		const u32 w = rd_be32 (raw + i);
		float f;
		memcpy (&f, &w, 4);
		if (((i - 8) & 0x3c) == 0)
			pcr_emit (&c, "%s\n    ", i > 8 ? "," : "");
		else
			pcr_emit (&c, ", ");
		if (gff_float_like (w))
			pcr_emit (&c, "%.9g", (double)f);
		else
			pcr_emit (&c, "%u", w);
	}
	pcr_emit (&c, "\n  ]\n}\n");
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT AI00:%s -> JSON:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT AI00:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
