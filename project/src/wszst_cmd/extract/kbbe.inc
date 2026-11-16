// We Cheer 2 (Wii) on-screen keyboard layouts ("subscreen/keydata.bin"), big-endian:
//   "KBBE", u32 table count, count * {u32 0, u32 first cell index}, then 4-byte cells {u8 column, u8 row, u16 UTF-16 code unit};
//   a table runs up to the next table's first cell (the last one to the end of the file) and ends with a {0xff, 0xff, 0, 0} terminator.
// Written as "<file>.tsv" with columns table, column, row, code (U+xxxx) and the character.
static void kbbe_utf8 (char *o, uint c)
{
	if (c < 0x80)
		*o++ = (char)c;
	else if (c < 0x800)
	{
		*o++ = (char)(0xc0 | c >> 6);
		*o++ = (char)(0x80 | (c & 0x3f));
	}
	else
	{
		*o++ = (char)(0xe0 | c >> 12);
		*o++ = (char)(0x80 | (c >> 6 & 0x3f));
		*o++ = (char)(0x80 | (c & 0x3f));
	}
	*o = 0;
}

static enumError extract_kbbe_file (ccp arg, ccp basedir, uint depth)
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
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 16 || memcmp (raw, "KBBE", 4))
		goto done;
	{
		const u32 n = rd_be32 (raw + 4);
		if (!n || n > 1000 || 8 + (u64)n * 8 > sz)
			goto done;
		const size_t base = 8 + (size_t)n * 8;
		const u32 cells = (u32)((sz - base) / 4);
		if ((sz - base) % 4)
			goto done;
		bool good = true;
		u32 prev = 0;
		for (u32 t = 0; t < n && good; t++)
		{
			const u32 s = rd_be32 (raw + 12 + (size_t)t * 8);
			if (rd_be32 (raw + 8 + (size_t)t * 8) || s < prev || s > cells)
				good = false;
			prev = s;
		}
		if (!good)
			goto done;
		pcr_ctx_t cx;
		memset (&cx, 0, sizeof (cx));
		cx.d = raw;
		cx.sz = sz;
		cx.ok = true;
		pcr_emit (&cx, "table\tcolumn\trow\tcode\tchar\n");
		for (u32 t = 0; t < n; t++)
		{
			const u32 s = rd_be32 (raw + 12 + (size_t)t * 8), e = t + 1 < n ? rd_be32 (raw + 12 + (size_t)(t + 1) * 8) : cells;
			for (u32 c = s; c < e; c++)
			{
				const u8 *p = raw + base + (size_t)c * 4;
				if (p[0] == 0xff && p[1] == 0xff)
					continue;
				const uint ch = p[2] << 8 | p[3];
				char u[8];
				kbbe_utf8 (u, ch < 0x20 || (ch >= 0xd800 && ch < 0xe000) ? '?' : ch);
				pcr_emit (&cx, "%u\t%u\t%u\tU+%04X\t%s\n", t, p[0], p[1], ch, u);
			}
		}
		if (cx.ok)
		{
			char dest[PATH_MAX];
			snprintf (dest, sizeof (dest), "%s.tsv", arg);
			if (testmode)
			{
				fprintf (stdlog, "WOULD EXTRACT KBBE:%s -> TSV:%s\n", arg, dest);
				err = ERR_OK;
			}
			else
			{
				File_t F;
				err = CreateFileOpt (&F, true, dest, false, arg);
				if (F.f && fwrite (cx.out, 1, cx.len, F.f) != cx.len)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)cx.len, dest);
				ResetFile (&F, opt_preserve);
				if (verbose >= 0 && !err)
					fprintf (stdlog, "%sEXTRACT KBBE:%s -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, dest);
			}
		}
		FREE (cx.out);
	}
done:
	FREE (raw);
	return err;
}
