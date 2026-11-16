// Marvelous "MSGP" message packages (Harvest Moon: Animal Parade, Wii: "msglang/<lang>/<sex>/*.mpc"), big-endian:
//   {"MSGP", u32 0x01000001, u32 entry count, u32 0x14 (table offset), u32 data offset}, count * {u32 id, u32 offset (relative to the data offset), u32 size}
//   and, per entry, an "MSGB" block {"MSGB", u32 0x01000001, u32 a, u32 b, u32 c, u32 d, u32 text offset (0x20), u32 text bytes} + the text as UTF-16BE
//   (control codes like "%k%" / "%1c%" are part of the text). Written as "<file>.tsv" with the columns id, a, b, c, d, text (newlines as "\n").
static enumError extract_msgp_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".mpc"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	if (sz < 0x20 || memcmp (raw, "MSGP", 4) || rd_be32 (raw + 4) != 0x01000001 || rd_be32 (raw + 0xc) != 0x14)
		goto done;
	{
		const u32 n = rd_be32 (raw + 8), data = rd_be32 (raw + 0x10);
		if (!n || n > 100000 || 0x14 + 12 * (u64)n != data || data > sz)
			goto done;
		for (u32 i = 0; i < n; i++)
		{
			const u8 *e = raw + 0x14 + 12 * (size_t)i;
			const u64 o = (u64)data + rd_be32 (e + 4), s = rd_be32 (e + 8);
			if (s < 0x20 || o + s > sz || memcmp (raw + o, "MSGB", 4))
				goto done;
			const u64 to = rd_be32 (raw + o + 0x18), tl = rd_be32 (raw + o + 0x1c);
			if (to + tl > s || (tl & 1))
				goto done;
		}
		c.d = raw;
		c.sz = sz;
		c.ok = true;
		pcr_emit (&c, "id\ta\tb\tc\td\ttext\n");
		for (u32 i = 0; i < n; i++)
		{
			const u8 *e = raw + 0x14 + 12 * (size_t)i;
			const u8 *b = raw + data + rd_be32 (e + 4);
			pcr_emit (&c, "%u\t%u\t%u\t%u\t%u\t", rd_be32 (e), rd_be32 (b + 8), rd_be32 (b + 12), rd_be32 (b + 16), rd_be32 (b + 20));
			const u8 *t = b + rd_be32 (b + 0x18);
			const u32 tl = rd_be32 (b + 0x1c) / 2;
			for (u32 k = 0; k < tl; k++)
			{
				uint ch = rd_be16 (t + 2 * k);
				if (ch >= 0xd800 && ch < 0xdc00 && k + 1 < tl)
				{
					const uint lo = rd_be16 (t + 2 * k + 2);
					if (lo >= 0xdc00 && lo < 0xe000)
					{
						ch = 0x10000 + ((ch - 0xd800) << 10) + (lo - 0xdc00);
						k++;
					}
				}
				if (ch == '\n')
					pcr_emit (&c, "\\n");
				else if (ch == '\t')
					pcr_emit (&c, "\\t");
				else if (ch == '\\')
					pcr_emit (&c, "\\\\");
				else if (ch < 0x20)
					pcr_emit (&c, "\\x%02x", ch);
				else if (ch < 0x80)
					pcr_emit (&c, "%c", (int)ch);
				else if (ch < 0x800)
					pcr_emit (&c, "%c%c", 0xc0 | (ch >> 6), 0x80 | (ch & 0x3f));
				else if (ch < 0x10000)
					pcr_emit (&c, "%c%c%c", 0xe0 | (ch >> 12), 0x80 | ((ch >> 6) & 0x3f), 0x80 | (ch & 0x3f));
				else
					pcr_emit (&c, "%c%c%c%c", 0xf0 | (ch >> 18), 0x80 | ((ch >> 12) & 0x3f), 0x80 | ((ch >> 6) & 0x3f), 0x80 | (ch & 0x3f));
			}
			pcr_emit (&c, "\n");
		}
		if (!c.ok)
			goto done;
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.tsv", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT MSGP:%s (%u messages) -> TSV:%s\n", arg, n, dest);
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
				fprintf (stdlog, "%sEXTRACT MSGP:%s (%u messages) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, n, dest);
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
