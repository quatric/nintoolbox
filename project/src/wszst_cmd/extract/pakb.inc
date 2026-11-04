// "PAKb" archive (Secret Files: Tunguska / Puritas Cordis, Wii): "PAKb", u32 BE count, then count 12-byte
// records {u32 name hash, u32 offset, u32 size}; payloads follow back to back from 8 + count * 12.
// Members are stored (THP movies, U8 archives, "MINA"/"GE"/"func" data, ...) and written as
// "<hash>.<ext>" with the extension taken from the payload magic. The archive is far above the in-memory
// limit, so members are streamed one at a time.
static ccp pakb_ext (const u8 *h, uint sz, char *buf, uint buf_sz)
{
	if (sz >= 4 && !memcmp (h, "THP\0", 4))
		return "thp";
	if (sz >= 4 && !memcmp (h, "OggS", 4))
		return "ogg";
	if (sz >= 4 && !memcmp (h, "RIFF", 4))
		return "wav";
	if (sz >= 4 && !memcmp (h, "U\xaa" "8-", 4))
		return "arc";
	if (sz >= 4 && !memcmp (h, "\x00\x20\xaf\x30", 4))
		return "tpl";
	if (sz >= 4 && !memcmp (h, "bres", 4))
		return "brres";
	if (sz >= 4 && isalpha (h[0]) && isalnum (h[1]) && isalnum (h[2]) && (isalnum (h[3]) || !h[3]))
	{
		uint k = 0;
		for (; k < 4 && h[k] && k + 1 < buf_sz; k++)
			buf[k] = tolower (h[k]);
		buf[k] = 0;
		return buf;
	}
	return "bin";
}

static enumError extract_pakb_file (ccp arg, ccp basedir, uint depth)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 hd[8];
	if (fread (hd, 1, 8, f) != 8 || memcmp (hd, "PAKb", 4) || fseeko (f, 0, SEEK_END))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	const u64 size = (u64)ftello (f);
	const u32 cnt = rd_be32 (hd + 4);
	if (cnt < 1 || cnt > 0x100000 || 8 + (u64)cnt * 12 > size || fseeko (f, 8, SEEK_SET))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	u8 *tab = MALLOC ((size_t)cnt * 12);
	if (!tab || fread (tab, 1, (size_t)cnt * 12, f) != (size_t)cnt * 12)
	{
		FREE (tab);
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	// payloads must start right after the table and stay inside the file
	u64 expect = 8 + (u64)cnt * 12;
	bool ok = true;
	for (u32 i = 0; i < cnt && ok; i++)
	{
		const u64 o = rd_be32 (tab + i * 12 + 4), s = rd_be32 (tab + i * 12 + 8);
		if (o != expect || o + s > size)
			ok = false;
		expect = o + s;
	}
	if (!ok)
	{
		FREE (tab);
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PAKB:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, cnt, dest);
	enumError err = ERR_OK;
	for (u32 i = 0; i < cnt && !err && !testmode; i++)
	{
		const u32 hash = rd_be32 (tab + i * 12), s = rd_be32 (tab + i * 12 + 8);
		const u64 o = rd_be32 (tab + i * 12 + 4);
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
			pakb_ext (buf, s, extbuf, sizeof (extbuf)));
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
