// Arctic Tale (Wii) "Language_<LANG>.lng" string tables, little-endian: "LANG", u32 version (3), u32 count, then count u32 offsets (the first is 0, an empty string) at 0x0c, then NUL terminated UTF-8
// strings; the offsets are relative to the end of the offset table. The key names come from the sibling "Language.ids" (a comma separated list
// in the same order) when it has the same number of entries. Written as "<file>.tsv" with columns index, id, text (\n, \t and \\ escaped); an offset outside the file gives an empty text.
static enumError extract_lngtab_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".lng"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *ids = 0;
	size_t sz = 0, isz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	if (sz < 0x20 || memcmp (raw, "LANG", 4) || rd_le32 (raw + 4) != 3)
		goto done;
	{
		const u32 cnt = rd_le32 (raw + 8);
		if (!cnt || cnt > 100000 || 12 + 4 * (u64)cnt > sz)
			goto done;
		const size_t pool = 12 + 4 * (size_t)cnt;
		// optional ids
		char idpath[PATH_MAX];
		snprintf (idpath, sizeof (idpath), "%s", arg);
		char *sl = strrchr (idpath, '/');
		snprintf (sl ? sl + 1 : idpath, sizeof (idpath) - (sl ? (size_t)(sl + 1 - idpath) : 0), "Language.ids");
		if (LoadFileAlloc (idpath, 0, 0, &ids, &isz, 0, 0, 0, false) || !ids)
			ids = 0;
		u32 nid = 0;
		for (size_t i = 0; ids && i < isz; i++)
			if (ids[i] == ',')
				nid++;
		const bool have_ids = ids && nid + 1 == cnt;
		c.d = raw;
		c.sz = sz;
		c.ok = true;
		pcr_emit (&c, "index\tid\ttext\n");
		size_t ip = 0;
		for (u32 i = 0; i < cnt; i++)
		{
			pcr_emit (&c, "%u\t", i);
			if (have_ids)
			{
				size_t e = ip;
				while (e < isz && ids[e] != ',' && ids[e] != '\r' && ids[e] != '\n')
					e++;
				pcr_emit (&c, "%.*s", (int)(e - ip), (ccp)ids + ip);
				ip = e + 1;
			}
			pcr_emit (&c, "\t");
			const u64 o = pool + (u64)rd_le32 (raw + 12 + 4 * (size_t)i);
			for (size_t p = o; o < sz && p < sz && raw[p]; p++)
			{
				if (raw[p] == '\n')
					pcr_emit (&c, "\\n");
				else if (raw[p] == '\t')
					pcr_emit (&c, "\\t");
				else if (raw[p] == '\\')
					pcr_emit (&c, "\\\\");
				else if (raw[p] == '\r')
					;
				else
					pcr_emit (&c, "%c", raw[p]);
			}
			pcr_emit (&c, "\n");
		}
		if (c.ok)
		{
			char dest[PATH_MAX];
			snprintf (dest, sizeof (dest), "%s.tsv", arg);
			if (testmode)
			{
				fprintf (stdlog, "WOULD EXTRACT LNG:%s -> TSV:%s\n", arg, dest);
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
					fprintf (stdlog, "%sEXTRACT LNG:%s -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, dest);
			}
		}
	}
done:
	FREE (c.out);
	FREE (ids);
	FREE (raw);
	return err;
}
