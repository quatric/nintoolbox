// "GFF0" effect database (Trauma Center: New Blood / Trauma Team, Wii: effect/gffData.bin), big-endian:
//   header {"GFF0", 0, u32 file size, u32 0x20, u32 0x20, u32 effect count, ...} then `count` effect blocks, each
//     +0x00..0x4f  effect header: +0x08 f32 (duration), +0x14 u32 block size (headers + parts), +0x20 u32 part count,
//                  +0x30 16 byte NUL padded name ("Effect001", "Eff056", ...)
//     +0x50..      `part count` part records, each starting 0x30 bytes before its 16 byte name "PartsNNN"; type 1 parts
//                  are 0x50 + 0x90 bytes plus a tail of 0x10 byte groups, other types are 0x40..0x50 bytes
// Part sizes are taken from the name positions (the size field was not identified). The words of every part are
// written unnamed (floats where the value looks like one) to "<name>.json"; the field meanings are not known.
static bool gff_float_like (u32 w)
{
	const uint e = (w >> 23) & 0xff;
	return w && e >= 0x60 && e <= 0x9a && w != 0x80000000u;
}

static void gff_words (pcr_ctx_t *c, const u8 *p, size_t n)
{
	for (size_t i = 0; i + 4 <= n; i += 4)
	{
		const u32 w = rd_be32 (p + i);
		float f;
		memcpy (&f, &w, 4);
		if (gff_float_like (w))
			pcr_emit (c, "%s%.9g", i ? ", " : "", (double)f);
		else
			pcr_emit (c, "%s%u", i ? ", " : "", w);
	}
}

static enumError extract_gff0_file (ccp arg, ccp basedir, uint depth)
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
	if (sz < 0x70 || memcmp (raw, "GFF0", 4) || rd_be32 (raw + 8) != sz || rd_be32 (raw + 12) != 0x20)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 ne = rd_be32 (raw + 20);
	// validate the chain of effect blocks
	size_t p = 0x20;
	u32 n = 0;
	while (n < ne && p + 0x50 <= sz)
	{
		const u32 bs = rd_be32 (raw + p + 0x14);
		if (bs < 0x50 || p + bs > sz)
			break;
		p += bs;
		n++;
	}
	if (n != ne || p != sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "{\n  \"effects\": [");
	p = 0x20;
	for (u32 i = 0; i < ne; i++)
	{
		const u32 bs = rd_be32 (raw + p + 0x14), np = rd_be32 (raw + p + 0x20);
		char nm[17];
		memcpy (nm, raw + p + 0x30, 16);
		nm[16] = 0;
		float dur;
		const u32 dw = rd_be32 (raw + p + 8);
		memcpy (&dur, &dw, 4);
		pcr_emit (&c, "%s\n    {\"name\": \"%s\", \"duration\": %.9g, \"size\": %u, \"parts\": [", i ? "," : "", nm, (double)dur, bs);
		// part starts: positions of a "PartsNNN" name minus 0x30
		size_t starts[512];
		uint ns = 0;
		for (size_t q = p + 0x50 + 0x30; q + 9 <= p + bs && ns < 511; q += 4)
			if (!memcmp (raw + q, "Parts", 5) && raw[q + 5] >= '0' && raw[q + 5] <= '9' && raw[q + 8] == 0)
				starts[ns++] = q - 0x30;
		starts[ns] = p + bs;
		for (uint k = 0; k < ns; k++)
		{
			char pn[17];
			memcpy (pn, raw + starts[k] + 0x30, 16);
			pn[16] = 0;
			const size_t plen = starts[k + 1] - starts[k];
			pcr_emit (&c, "%s\n      {\"name\": \"%s\", \"type\": %u, \"size\": %u, \"words\": [", k ? "," : "", pn,
				rd_be32 (raw + starts[k]), (uint)plen);
			gff_words (&c, raw + starts[k], plen);
			pcr_emit (&c, "]}");
		}
		pcr_emit (&c, "\n    ]%s}", ns == np ? "" : ", \"part_count_mismatch\": true");
		p += bs;
	}
	pcr_emit (&c, "\n  ]\n}\n");
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT GFF0:%s -> JSON:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT GFF0:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
