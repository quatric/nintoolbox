// "ADEV" adventure event tables (Soul Eater: Monotone Princess, Wii: event/AdvEvent*.bin, AdvRandomEvent*.bin), big-endian:
//   {"ADEV", u32 header size (16 + 8 * group count), u32 8 * total event count, u32 group count,
//    group count * u32 first event index, group count * u32 event count}, then total * 0xA00 bytes: every event is
//   40 records of 0x40 bytes (32 u16 words, 0xffff = unused). The word meanings are not known; written as
//   "<file>.json" with the words unnamed (0xffff as -1).
static enumError extract_adev_file (ccp arg, ccp basedir, uint depth)
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
	const u32 hs = sz >= 16 ? rd_be32 (raw + 4) : 0, ng = sz >= 16 ? rd_be32 (raw + 12) : 0;
	u32 total = 0;
	bool good = sz >= 16 && !memcmp (raw, "ADEV", 4) && ng && ng < 256 && hs == 16 + 8 * ng && hs <= sz;
	for (u32 g = 0; good && g < ng; g++)
	{
		if (rd_be32 (raw + 16 + 4 * g) != total)
			good = false;
		total += rd_be32 (raw + 16 + 4 * ng + 4 * g);
	}
	good = good && total && rd_be32 (raw + 8) == 8 * total && sz == hs + (size_t)total * 0xa00;
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
	pcr_emit (&c, "{\n  \"groups\": [");
	u32 ev = 0;
	for (u32 g = 0; g < ng; g++)
	{
		const u32 cnt = rd_be32 (raw + 16 + 4 * ng + 4 * g);
		pcr_emit (&c, "%s\n    {\"first_event\": %u, \"events\": [", g ? "," : "", ev);
		for (u32 e = 0; e < cnt; e++, ev++)
		{
			pcr_emit (&c, "%s\n      [", e ? "," : "");
			for (uint r = 0; r < 40; r++)
			{
				const u8 *p = raw + hs + (size_t)ev * 0xa00 + (size_t)r * 0x40;
				pcr_emit (&c, "%s[", r ? ", " : "");
				for (uint k = 0; k < 32; k++)
				{
					const uint w = rd_be16 (p + 2 * k);
					pcr_emit (&c, "%s%d", k ? "," : "", w == 0xffff ? -1 : (int)w);
				}
				pcr_emit (&c, "]");
			}
			pcr_emit (&c, "]");
		}
		pcr_emit (&c, "]}");
	}
	pcr_emit (&c, "\n  ]\n}\n");
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT ADEV:%s -> JSON:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT ADEV:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
#pragma pop_macro("free")
#pragma pop_macro("calloc")
