// "KP0" package (".pkt": Power Punch, Atari's Wii boxing game; nested packages too): little endian {"KP0\0", u32 count, u32 data start, u32 entry
// size (24)}, count entries {u32 name offset (into the string table behind the entries), u32 stored size, u32 offset (from the data start),
// u32 flags (low 16 bits: 1 = raw, 0x1e01 / 0x1c01 = compressed with a Nintendo BIOS header on the data: 0x10 LZ10, 0x11 LZ11,
// 0x30 RLE; 2 / 3 = external reference by path, no data), u32 unpacked size, u32 name length + 1}. Members are written under their names
// in "<file>.d" (references are listed in "references.txt").
static bool kp0_rle (u8 *out, uint out_len, const u8 *s, uint n)
{
	uint sp = 4, dp = 0;
	while (dp < out_len)
	{
		if (sp >= n)
			return false;
		const uint c = s[sp++], len = (c & 0x7f) + ((c & 0x80) ? 3 : 1);
		if (dp + len > out_len || sp + ((c & 0x80) ? 1 : len) > n)
			return false;
		if (c & 0x80)
			memset (out + dp, s[sp++], len);
		else
			memcpy (out + dp, s + sp, len), sp += len;
		dp += len;
	}
	return true;
}

static enumError extract_kp0_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x20 || memcmp (raw, "KP0", 4) || rd_le32 (raw + 12) != 24)
		goto done;
	{
		const uint n = rd_le32 (raw + 4), st = rd_le32 (raw + 8);
		if (n > 100000 || 16 + 24 * (u64)n > st || st > sz)
			goto done;
		const uint nt = 16 + 24 * n;
		for (uint i = 0; i < n; i++)
		{
			const u8 *e = raw + 16 + 24 * i;
			const u64 no = (u64)nt + rd_le32 (e);
			if (no >= st || (rd_le32 (e + 12) & 0xffff) < 1 || (rd_le32 (e + 12) & 0xffff) > 0x1fff
				|| ((rd_le32 (e + 12) & 0xffff) < 2 && st + (u64)rd_le32 (e + 8) + rd_le32 (e + 4) > sz))
				goto done;
			if (!memchr (raw + no, 0, st - no))
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT KP0:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		char refs[8192];
		uint rl = 0;
		refs[0] = 0;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = raw + 16 + 24 * i;
			const u32 sz1 = rd_le32 (e + 4), off = rd_le32 (e + 8), fl = rd_le32 (e + 12) & 0xffff, usz = rd_le32 (e + 16);
			char name[PATH_MAX];
			snprintf (name, sizeof (name), "%s", (ccp)raw + nt + rd_le32 (e));
			for (char *p = name; *p; p++)
				if (*p == '\\' || *p == ':')
					*p = '/';
			if (fl >= 2 && fl < 4)
			{
				rl += snprintf (refs + rl, sizeof (refs) - rl > 0 ? sizeof (refs) - rl : 0, "%s\n", name);
				if (rl >= sizeof (refs) - 300)
					rl = sizeof (refs) - 300;
				continue;
			}
			const char *safe = name;
			while (!strncmp (safe, "../", 3) || *safe == '/')
				safe += *safe == '/' ? 1 : 3;
			if (!*safe)
				snprintf (name, sizeof (name), "%03u.bin", i), safe = name;
			const u8 *d = raw + st + off;
			u8 *tmp = 0;
			uint tsz = 0;
			if (fl != 1 && sz1 >= 4 && (d[0] == 0x10 || d[0] == 0x11))
			{
				if (DecodeLZ10LZ11 (&tmp, &tsz, d, sz1))
					tmp = 0;
			}
			else if (fl != 1 && sz1 >= 4 && d[0] == 0x30)
			{
				tsz = d[1] | d[2] << 8 | d[3] << 16;
				tmp = tsz && tsz < 0x10000000 ? MALLOC (tsz) : 0;
				if (tmp && !kp0_rle (tmp, tsz, d, sz1))
					FREE (tmp), tmp = 0;
			}
			if (tmp)
			{
				err = shade_write (arg, dest, basedir, safe, tmp, tsz);
				FREE (tmp);
			}
			else
				err = shade_write (arg, dest, basedir, safe, d, sz1);
			(void)usz;
		}
		if (!err && !testmode && rl)
			err = shade_write (arg, dest, basedir, "references.txt", refs, rl);
	}
done:
	FREE (raw);
	return err;
}
