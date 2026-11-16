// Earth Seeker (Wii) "FFX/*.ffx" message files, big-endian "SYSM": {"SYSM", u8 1, u8 1, u16 0, u32 0x20, u32 text pool offset, u32 0x20, u32 pair count, ...}; from 0x20 an ascending table of 2 * pairs u32 offsets
// into the pool: entry pair i = label [o[2i], o[2i+1]) and message [o[2i+1], o[2i+2]) (the last one runs to the end of the file), both NUL-terminated Shift-JIS (the messages hold ^t[..] / ^c[..] control tags, kept as they are).
// Written as "<file>.tsv" (label, text; \n, \t, \\ escaped).
static void ffx_emit_sjis (pcr_ctx_t *c, const u8 *s, size_t n)
{
	cucp src = s, end = s + n;
	while (src < end && *src)
	{
		cucp before = src;
		const int code = ScanShiftJISCharE (&src, end);
		if (code <= 0)
		{
			pcr_emit (c, "\\x%02x", *before);
			src = before + 1;
			continue;
		}
		if (code == '\n')
			pcr_emit (c, "\\n");
		else if (code == '\t')
			pcr_emit (c, "\\t");
		else if (code == '\\')
			pcr_emit (c, "\\\\");
		else if (code == '\r')
			;
		else
		{
			char buf[8], *e = PrintUTF8Char (buf, code);
			*e = 0;
			pcr_emit (c, "%s", buf);
		}
	}
}

static enumError extract_ffxsysm_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".ffx"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	const u32 pool = sz >= 0x30 ? rd_be32 (raw + 12) : 0, pairs = sz >= 0x30 ? rd_be32 (raw + 0x14) : 0;
	if (sz < 0x30 || memcmp (raw, "SYSM", 4) || rd_be32 (raw + 8) != 0x20 || pool < 0x28 || pool >= sz || !pairs || pairs > 100000 || 0x20 + 8 * (u64)pairs > pool)
		goto done;
	{
		u32 prev = 0;
		for (u32 i = 0; i < 2 * pairs; i++)
		{
			const u32 o = rd_be32 (raw + 0x20 + 4 * (size_t)i);
			if (o < prev || pool + (u64)o > sz)
				goto done;
			prev = o;
		}
		c.d = raw;
		c.sz = sz;
		c.ok = true;
		pcr_emit (&c, "label\ttext\n");
		for (u32 i = 0; i < pairs; i++)
		{
			const u32 a = rd_be32 (raw + 0x20 + 8 * (size_t)i), b = rd_be32 (raw + 0x24 + 8 * (size_t)i), e = i + 1 < pairs ? rd_be32 (raw + 0x28 + 8 * (size_t)i) : (u32)(sz - pool);
			ffx_emit_sjis (&c, raw + pool + a, b - a);
			pcr_emit (&c, "\t");
			ffx_emit_sjis (&c, raw + pool + b, e - b);
			pcr_emit (&c, "\n");
		}
		if (c.ok)
		{
			char dest[PATH_MAX];
			snprintf (dest, sizeof (dest), "%s.tsv", arg);
			if (testmode)
			{
				fprintf (stdlog, "WOULD EXTRACT FFX:%s -> TSV:%s\n", arg, dest);
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
					fprintf (stdlog, "%sEXTRACT FFX:%s (%u messages) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, pairs, dest);
			}
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
