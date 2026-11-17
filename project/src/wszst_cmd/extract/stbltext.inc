// Harvest Moon: Animal Parade (Wii) "*.stb" (STBL) string tables, big-endian: {"STBL", u32 0x01000001, u32 entry count, u32 string width in bytes (0x40), u32 entry table offset (0x18), u32 data offset}, entries
// of 16 bytes {u32 id, u32 0, u32 string count, u32 data offset of the first string} and fixed-width NUL-padded UTF-16BE strings. Written as "<file>.tsv" (id, index, text; \n, \t, \\ escaped).
static enumError extract_stbl_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".stb"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	if (sz < 0x20 || memcmp (raw, "STBL", 4))
		goto done;
	{
		const u32 ne = rd_be32 (raw + 8), w = rd_be32 (raw + 12), eo = rd_be32 (raw + 16), dof = rd_be32 (raw + 20);
		if (!ne || ne > 100000 || w < 2 || w > 0x1000 || (w & 1) || eo < 0x18 || eo + 16 * (u64)ne > sz || dof > sz)
			goto done;
		c.d = raw;
		c.sz = sz;
		c.ok = true;
		pcr_emit (&c, "id\tindex\ttext\n");
		uint n = 0;
		for (uint i = 0; i < ne; i++)
		{
			const u8 *e = raw + eo + 16 * (size_t)i;
			const u32 id = rd_be32 (e), cnt = rd_be32 (e + 8), off = rd_be32 (e + 12);
			if (cnt > 100000 || dof + (u64)off + (u64)cnt * w > sz)
				continue;
			for (uint k = 0; k < cnt; k++)
			{
				const u8 *s = raw + dof + off + (size_t)k * w;
				uint len = 0;
				while (len < w / 2 && rd_be16 (s + 2 * len))
					len++;
				pcr_emit (&c, "%u\t%u\t", id, k);
				for (uint j = 0; j < len; j++)
				{
					uint u = rd_be16 (s + 2 * j);
					if (u >= 0xd800 && u < 0xdc00 && j + 1 < len)
						u = 0x10000 + ((u - 0xd800) << 10) + (rd_be16 (s + 2 * j + 2) - 0xdc00), j++;
					if (u == '\n')
						pcr_emit (&c, "\\n");
					else if (u == '\t')
						pcr_emit (&c, "\\t");
					else if (u == '\\')
						pcr_emit (&c, "\\\\");
					else if (u == '\r')
						;
					else if (u < 0x80)
						pcr_emit (&c, "%c", (int)u);
					else if (u < 0x800)
						pcr_emit (&c, "%c%c", (int)(0xc0 | u >> 6), (int)(0x80 | (u & 63)));
					else if (u < 0x10000)
						pcr_emit (&c, "%c%c%c", (int)(0xe0 | u >> 12), (int)(0x80 | (u >> 6 & 63)), (int)(0x80 | (u & 63)));
					else
						pcr_emit (&c, "%c%c%c%c", (int)(0xf0 | u >> 18), (int)(0x80 | (u >> 12 & 63)), (int)(0x80 | (u >> 6 & 63)), (int)(0x80 | (u & 63)));
				}
				pcr_emit (&c, "\n");
				n++;
			}
		}
		if (!n)
			goto done;
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.tsv", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT STBL:%s -> TSV:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT STBL:%s (%u strings) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, n, dest);
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
