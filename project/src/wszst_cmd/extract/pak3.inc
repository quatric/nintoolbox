// "PAK\3" archive (International Athletics `GameData.pak`, 138 MB): "PAK\3", u32 LE count, u32 1, then count
// 140-byte records {char name[128] with backslash paths, u32 stored size, u32 size, u32 offset}; payloads follow
// the table. Members with stored size == size are raw, the others use a byte-oriented LZSS: after a 4-byte
// member header the stream is made of u16 LE control words (LSB first); a clear bit is a literal byte, a set bit
// a 2-byte match {b0, b1}: length (b0 & 15) + 1, distance b1 | (b0 >> 4) << 8 (12 bit, back from the output end).
// The archive is streamed one member at a time.
static bool pak3_unlz (const u8 *s, size_t ss, u8 *d, size_t ds)
{
	size_t i = 4, o = 0;
	uint ctl = 0, bits = 0;
	while (o < ds)
	{
		if (!bits)
		{
			if (i + 2 > ss)
				return false;
			ctl = s[i] | (s[i + 1] << 8);
			i += 2;
			bits = 16;
		}
		const uint bit = ctl & 1;
		ctl >>= 1;
		bits--;
		if (!bit)
		{
			if (i >= ss)
				return false;
			d[o++] = s[i++];
		}
		else
		{
			if (i + 2 > ss)
				return false;
			const uint b0 = s[i], b1 = s[i + 1];
			i += 2;
			const size_t dist = b1 | ((b0 >> 4) << 8);
			if (!dist || dist > o)
				return false;
			for (uint n = (b0 & 15) + 1; n && o < ds; n--, o++)
				d[o] = d[o - dist];
		}
	}
	return true;
}

static enumError extract_pak3_file (ccp arg, ccp basedir, uint depth)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 hd[12];
	if (fread (hd, 1, 12, f) != 12 || memcmp (hd, "PAK\3", 4) || rd_le32 (hd + 8) != 1 || fseeko (f, 0, SEEK_END))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	const u64 size = (u64)ftello (f);
	const u32 cnt = rd_le32 (hd + 4);
	if (cnt < 1 || cnt > 0x100000 || 12 + (u64)cnt * 140 > size || fseeko (f, 12, SEEK_SET))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	u8 *tab = MALLOC ((size_t)cnt * 140);
	if (!tab || fread (tab, 1, (size_t)cnt * 140, f) != (size_t)cnt * 140)
	{
		FREE (tab);
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	for (u32 i = 0; i < cnt; i++)
	{
		const u8 *r = tab + 140 * (size_t)i;
		const u64 ss = rd_le32 (r + 128), us = rd_le32 (r + 132), o = rd_le32 (r + 136);
		if (!r[0] || o + ss > size || ss > us || us > 0x40000000)
		{
			FREE (tab);
			fclose (f);
			return ERR_NOTHING_TO_DO;
		}
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PAK3:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, dest);
	enumError err = ERR_OK;
	for (u32 i = 0; i < cnt && !err && !testmode; i++)
	{
		const u8 *r = tab + 140 * (size_t)i;
		const u32 ss = rd_le32 (r + 128), us = rd_le32 (r + 132), o = rd_le32 (r + 136);
		u8 *sbuf = MALLOC (ss ? ss : 1);
		u8 *dbuf = ss == us ? sbuf : MALLOC (us ? us : 1);
		if (!sbuf || !dbuf || fseeko (f, (off_t)o, SEEK_SET) || (ss && fread (sbuf, 1, ss, f) != ss))
		{
			if (dbuf != sbuf)
				FREE (dbuf);
			FREE (sbuf);
			err = ERR_READ_FAILED;
			break;
		}
		bool ok = true;
		if (ss != us)
			ok = pak3_unlz (sbuf, ss, dbuf, us);
		char name[129];
		memcpy (name, r, 128);
		name[128] = 0;
		for (char *c = name; *c; c++)
			if (*c == '\\')
				*c = '/';
		const char *nm = name;
		while (*nm == '/')
			nm++;
		if (ok && *nm && !strstr (nm, ".."))
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", nm);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && us && fwrite (dbuf, 1, us, F.f) != us)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", us, path);
			ResetFile (&F, opt_preserve);
		}
		else if (!ok)
			fprintf (stdlog, "!! PAK3: member %s is corrupt, skipped\n", nm);
		if (dbuf != sbuf)
			FREE (dbuf);
		FREE (sbuf);
	}
	FREE (tab);
	fclose (f);
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
