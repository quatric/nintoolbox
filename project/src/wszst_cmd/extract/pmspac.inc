// Dog Island (Artlist Collection, Wii) "pms/*.pac", "pms/*.tex" and extensionless "chara/<n>" containers, big-endian:
// {u32 count, u32 0x10000, u32 0, u32 0x10}, count 0x20-byte records {char name[16], char type[4], u32 size, u32 offset, u32 0}, payloads at the stored offsets.
// Members are written as "<name>.<type>" (types seen: tpl, ymg, tex, YOBJ models, DUMY placeholders); embedded TPLs are extracted recursively.
static enumError extract_pmspac_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pac") && !is_ext (arg, ".tex") && strrchr (arg, '.') > strrchr (arg, '/'))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x30 || rd_be32 (raw + 4) != 0x10000 || rd_be32 (raw + 8) || rd_be32 (raw + 12) != 0x10)
		goto done;
	{
		const u32 n = rd_be32 (raw);
		if (!n || n > 4096 || 0x10 + (u64)n * 0x20 > sz)
			goto done;
		for (u32 i = 0; i < n; i++)
		{
			const u8 *r = raw + 0x10 + 0x20 * (size_t)i;
			if (!r[0] || r[15] || r[16] < 0x20 || r[16] > 0x7e || r[19] || rd_be32 (r + 28)
				|| (u64)rd_be32 (r + 20) + rd_be32 (r + 24) > sz || rd_be32 (r + 24) < 0x10 + (u64)n * 0x20)
				goto done;
			for (uint k = 0; k < 15 && r[k]; k++)
				if (r[k] < 0x20 || r[k] > 0x7e || r[k] == '/' || r[k] == '\\')
					goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT PMSPAC:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (u32 i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *r = raw + 0x10 + 0x20 * (size_t)i;
			char name[64];
			snprintf (name, sizeof (name), "%02u_%.15s.%.3s", i, (ccp)r, (ccp)r + 16);
			err = shade_write (arg, dest, basedir, name, raw + rd_be32 (r + 24), rd_be32 (r + 20));
		}
		if (!err && !testmode)
		{
			enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (raw);
	return err;
}
