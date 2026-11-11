// J3D texture pattern animation "BTP" ("J3D1btp1" + TPT1 chunk, big endian): u8 loop (+8), u16 duration (+0x0a), u16 entry count (+0x0c), u16 texture index
// count (+0x0e), u32 offsets: entries +0x10, texture index table (u16) +0x14. Every 8 byte entry is {u16 frame count, u16 first index, u8 texture slot, 3 pad}:
// frame k shows texture index table[first + k]. Written as "<name>.csv": entry,slot,frame,texture_index.
static enumError extract_j3dbtp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".btp") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 0x30 && !memcmp (raw, "J3D1btp1", 8) && !memcmp (raw + 0x20, "TPT1", 4))
	{
		const u8 *a = raw + 0x20;
		const size_t asz = rd_be32 (a + 4);
		const uint dur = a[10] << 8 | a[11], ne = a[12] << 8 | a[13], nt = a[14] << 8 | a[15];
		const u32 eo = rd_be32 (a + 0x10), to = rd_be32 (a + 0x14);
		bool ok = asz >= 0x18 && 0x20 + asz <= sz && ne <= 4096 && eo + (size_t)ne * 8 <= asz && to + (size_t)nt * 2 <= asz;
		bcsv_buf_t b = {0, 0, 0};
		char tmp[128];
		int n = snprintf (tmp, sizeof (tmp), "entry,slot,frame,texture_index\n# duration %u loop %u\n", dur, a[8]);
		bcsv_put (&b, tmp, n);
		for (uint e = 0; e < ne && ok; e++)
		{
			const u8 *p = a + eo + e * 8;
			const uint cnt = p[0] << 8 | p[1], first = p[2] << 8 | p[3];
			if (first + cnt > nt)
			{
				ok = false;
				break;
			}
			for (uint k = 0; k < cnt; k++)
			{
				const u8 *t = a + to + (first + k) * 2;
				n = snprintf (tmp, sizeof (tmp), "%u,%u,%u,%u\n", e, p[4], k, t[0] << 8 | t[1]);
				bcsv_put (&b, tmp, n);
			}
		}
		if (ok && b.p)
		{
			char stem[PATH_MAX], fn[PATH_MAX], dest[PATH_MAX];
			const char *sl = strrchr (arg, '/');
			snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (stem, '.');
			if (dot)
				*dot = 0;
			snprintf (fn, sizeof (fn), "%s.csv", stem);
			beside_source_dest (dest, sizeof (dest), arg);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT J3D-BTP:%s -> %s/%s%s (%u entries)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", fn, ne);
			err = testmode ? ERR_OK : shade_write (arg, dest, basedir, fn, (u8 *)b.p, (uint)b.n);
		}
		FREE (b.p);
	}
	FREE (raw);
	return err;
}
