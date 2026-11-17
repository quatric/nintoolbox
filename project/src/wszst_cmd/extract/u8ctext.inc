// Bass Fishing Wii / Big Catch "text/*.u8c" string container, big-endian: {"U8C0", u32 0x10, u32 file size, u32 0}, then chunks {tag, u32 size incl. 16-byte header, u32 value, u32 0}. The "STRS" chunk (value =
// string count) holds that many NUL-terminated UTF-8 strings back to back (all languages one after another); the "KIND", "CROW" ... chunks index them. Written as "<file>.tsv" (index, byte offset in the pool,
// text; \n, \t, \\ escaped).
static enumError extract_u8ctext_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".u8c"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	if (sz < 0x30 || memcmp (raw, "U8C0", 4) || rd_be32 (raw + 4) != 0x10 || rd_be32 (raw + 8) != sz || memcmp (raw + 0x10, "STRS", 4))
		goto done;
	{
		const u32 ssz = rd_be32 (raw + 0x14), n = rd_be32 (raw + 0x18);
		if (ssz < 0x11 || 0x10 + (u64)ssz > sz || !n || n > 1000000)
			goto done;
		const u8 *pool = raw + 0x20;
		const size_t pl = ssz - 0x10;
		c.d = raw;
		c.sz = sz;
		c.ok = true;
		pcr_emit (&c, "index\toffset\ttext\n");
		size_t p = 0;
		for (u32 i = 0; i < n; i++)
		{
			if (p >= pl)
				goto done;
			pcr_emit (&c, "%u\t%u\t", i, (uint)p);
			for (; p < pl && pool[p]; p++)
			{
				const u8 ch = pool[p];
				if (ch == '\n')
					pcr_emit (&c, "\\n");
				else if (ch == '\t')
					pcr_emit (&c, "\\t");
				else if (ch == '\\')
					pcr_emit (&c, "\\\\");
				else if (ch == '\r')
					;
				else
					pcr_emit (&c, "%c", ch);
			}
			p++;
			pcr_emit (&c, "\n");
		}
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.tsv", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT U8C:%s -> TSV:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT U8C:%s (%u strings) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, n, dest);
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
