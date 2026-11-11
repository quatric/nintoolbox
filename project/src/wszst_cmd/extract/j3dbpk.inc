// J3D material colour animation "BPK" ("J3D1bpk1" + PAK1 chunk, big endian): u8 loop, u16 duration (+0x0a), u16 entry count (+0x0c), u16 R/G/B/A table sizes
// (+0x0e..+0x14), u32 offsets: entries +0x18, R/G/B/A s16 tables +0x24/0x28/0x2c/0x30. Every entry is four tracks (R G B A) of {u16 key count, u16 table index,
// u16 tangent type}; a single key is a constant, otherwise keys are {time, value, tangent in[, out]} (3 or 4 values when the tangent type is 1).
// Written as "<name>.csv": entry,channel,time,value (colour components 0..255).
static enumError extract_j3dbpk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bpk") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 0x60 && !memcmp (raw, "J3D1bpk1", 8) && !memcmp (raw + 0x20, "PAK1", 4))
	{
		const u8 *a = raw + 0x20;
		const size_t asz = rd_be32 (a + 4);
		const uint dur = a[10] << 8 | a[11], ne = a[12] << 8 | a[13];
		uint cntv[4];
		u32 tabo[4];
		bool ok = asz >= 0x34 && 0x20 + asz <= sz && ne <= 4096;
		const u32 eo = rd_be32 (a + 0x18);
		for (uint i = 0; i < 4; i++)
		{
			cntv[i] = a[14 + i * 2] << 8 | a[15 + i * 2];
			tabo[i] = rd_be32 (a + 0x24 + i * 4);
			ok = ok && tabo[i] + (size_t)cntv[i] * 2 <= asz;
		}
		ok = ok && eo + (size_t)ne * 24 <= asz;
		bcsv_buf_t b = {0, 0, 0};
		char tmp[128];
		int n = snprintf (tmp, sizeof (tmp), "entry,channel,time,value\n# duration %u loop %u\n", dur, a[8]);
		bcsv_put (&b, tmp, n);
		static const char *const chn[4] = {"r", "g", "b", "a"};
		for (uint e = 0; e < ne && ok; e++)
			for (uint c = 0; c < 4 && ok; c++)
			{
				const u8 *t = a + eo + e * 24 + c * 6;
				const uint cnt = t[0] << 8 | t[1], idx = t[2] << 8 | t[3], tt = t[4] << 8 | t[5];
				const uint stride = tt == 1 ? 4 : 3;
				if (idx + (cnt == 1 ? 1 : cnt * stride) > cntv[c])
				{
					ok = false;
					break;
				}
				const u8 *tab = a + tabo[c];
				for (uint k = 0; k < cnt; k++)
				{
					const uint at = cnt == 1 ? idx : idx + k * stride;
					const int v = (int16_t)(tab[(cnt == 1 ? at : at + 1) * 2] << 8 | tab[(cnt == 1 ? at : at + 1) * 2 + 1]);
					const int tm = cnt == 1 ? 0 : (int16_t)(tab[at * 2] << 8 | tab[at * 2 + 1]);
					n = snprintf (tmp, sizeof (tmp), "%u,%s,%d,%d\n", e, chn[c], tm, v);
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
				fprintf (stdlog, "%s%sEXTRACT J3D-BPK:%s -> %s/%s%s (%u entries)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", fn, ne);
			err = testmode ? ERR_OK : shade_write (arg, dest, basedir, fn, (u8 *)b.p, (uint)b.n);
		}
		FREE (b.p);
	}
	FREE (raw);
	return err;
}
