// 49Games ".pak" (Summer/Winter Athletics series: Summer Athletics, Summer Stars, Winter Sports, World
// Championship Athletics; Wii): LE header {u32 0xDEADBEEF, u32 count, u32 index end (0x14 + count * 40), u32 1,
// u32 0} then count 40-byte records {u32 hash, u32 ?, u32 offset, u32 0, u32 stored size, u32 0,
// u32 unpacked size, u32 0, u32 ?, u32 0}. Members whose stored size equals the unpacked size are raw
// (THP movies, "WSBL" sound banks, ...); the rest use an unidentified bit-packed codec and are written as
// ".packed" so nothing is lost. Streamed member by member (the paks reach 4 GB).
static ccp ws49pak_ext (const u8 *h, uint sz, bool packed, char *buf, uint buf_sz)
{
	if (packed)
		return "packed";
	if (sz >= 4 && !memcmp (h, "THP\0", 4))
		return "thp";
	if (sz >= 4 && !memcmp (h, "OggS", 4))
		return "ogg";
	if (sz >= 4 && !memcmp (h, "RIFF", 4))
		return "wav";
	if (sz >= 4 && !memcmp (h, "U\xaa" "8-", 4))
		return "arc";
	if (sz >= 4 && isalpha (h[0]) && isalnum (h[1]) && isalnum (h[2]) && isalnum (h[3]))
	{
		for (uint k = 0; k < 4 && k + 1 < buf_sz; k++)
			buf[k] = tolower (h[k]), buf[k + 1] = 0;
		return buf;
	}
	return "bin";
}

static enumError extract_ws49pak_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pak"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 hd[0x14];
	if (fread (hd, 1, 0x14, f) != 0x14 || rd_le32 (hd) != 0xdeadbeef || fseeko (f, 0, SEEK_END))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	const u64 size = (u64)ftello (f);
	const u32 cnt = rd_le32 (hd + 4);
	if (cnt < 1 || cnt > 0x100000 || rd_le32 (hd + 8) != 0x14 + (u64)cnt * 40 || rd_le32 (hd + 12) != 1
	    || rd_le32 (hd + 8) > size || fseeko (f, 0x14, SEEK_SET))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	u8 *tab = MALLOC ((size_t)cnt * 40);
	if (!tab || fread (tab, 1, (size_t)cnt * 40, f) != (size_t)cnt * 40)
	{
		FREE (tab);
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	for (u32 i = 0; i < cnt; i++)
	{
		const u8 *r = tab + i * 40;
		if (rd_le32 (r + 8) < rd_le32 (hd + 8) || (u64)rd_le32 (r + 8) + rd_le32 (r + 16) > size)
		{
			FREE (tab);
			fclose (f);
			return ERR_NOTHING_TO_DO;
		}
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT 49PAK:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, cnt, dest);
	enumError err = ERR_OK;
	for (u32 i = 0; i < cnt && !err && !testmode; i++)
	{
		const u8 *r = tab + i * 40;
		const u32 hash = rd_le32 (r), s = rd_le32 (r + 16), usz = rd_le32 (r + 24);
		const u64 o = rd_le32 (r + 8);
		u8 *buf = MALLOC (s ? s : 1);
		if (!buf || fseeko (f, (off_t)o, SEEK_SET) || (s && fread (buf, 1, s, f) != s))
		{
			FREE (buf);
			err = ERR_READ_FAILED;
			break;
		}
		char extbuf[8];
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%08x_%05u.%s", dest, basedir ? basedir : "", hash, i,
			ws49pak_ext (buf, s, s != usz, extbuf, sizeof (extbuf)));
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && s && fwrite (buf, 1, s, F.f) != s)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
		ResetFile (&F, opt_preserve);
		FREE (buf);
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
