// "BCAB" collision chunk list (Paws & Claws, Wii: TPWiiProj/COL/merged_cs|dirt|hoofs.bin), big-endian:
//   a chain of chunks {char name[4] ("DOGA", "DRTC", "H_HL", ...), u32 body size, body} that ends exactly at EOF;
//   every body is the tag "BCAB" followed by (size - 4) / 4 floats (10 normally, 11 for "DRTC").
// The meaning of the floats is not known. Written as "<file>.json".
static enumError extract_bcabchunks_file (ccp arg, ccp basedir, uint depth)
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
	bool good = sz >= 16;
	size_t p = 0;
	uint n = 0;
	while (good && p < sz)
	{
		if (p + 16 > sz)
		{
			good = false;
			break;
		}
		const u32 bs = rd_be32 (raw + p + 4);
		for (uint k = 0; k < 4; k++)
			if (raw[p + k] < 0x20 || raw[p + k] > 0x7e || raw[p + k] == '"' || raw[p + k] == '\\')
				good = false;
		if (!good || bs < 8 || (bs & 3) || p + 8 + bs > sz || memcmp (raw + p + 8, "BCAB", 4))
		{
			good = false;
			break;
		}
		p += 8 + bs;
		n++;
	}
	if (!good || !n)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "{\n  \"format\": \"BCAB chunks\",\n  \"chunks\": [");
	p = 0;
	for (uint i = 0; i < n; i++)
	{
		const u32 bs = rd_be32 (raw + p + 4);
		pcr_emit (&c, "%s\n    {\"name\": \"%.4s\", \"values\": [", i ? "," : "", (const char *)raw + p);
		for (u32 k = 4; k < bs; k += 4)
		{
			const u32 w = rd_be32 (raw + p + 8 + k);
			float f;
			memcpy (&f, &w, 4);
			pcr_emit (&c, "%s%.9g", k > 4 ? ", " : "", (double)f);
		}
		pcr_emit (&c, "]}");
		p += 8 + bs;
	}
	pcr_emit (&c, "\n  ]\n}\n");
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT BCABCHK:%s -> JSON:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT BCABCHK:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
