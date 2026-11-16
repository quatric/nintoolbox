// "VLIB" variable library (Meet the Robinsons, Wii: data/var/**/*.lib), little-endian: {"VLIB", u32 0x100, u32 count, u32 payload bytes, u32 319-style index entry count, u32 index bytes}, then from 0x18
// count records {u16 length, name\0 (e.g. "var/weapons/aiming0.var"), data (length - name bytes - 1)}; the records end at 0x18 + payload bytes and a trailing index (not decoded) fills the rest. The member data
// is a tokenised binary form of the ".var" text files; it is written unchanged under its path in "<file>.d/".
static enumError extract_vliblib_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".lib"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	const u32 cnt = sz >= 0x18 ? rd_le32 (raw + 8) : 0, pay = sz >= 0x18 ? rd_le32 (raw + 12) : 0;
	if (sz < 0x18 || memcmp (raw, "VLIB", 4) || !cnt || cnt > 100000 || 0x18 + (u64)pay > sz)
		goto done;
	{
		size_t p = 0x18;
		for (u32 i = 0; i < cnt; i++)
		{
			if (p + 3 > 0x18 + (size_t)pay)
				goto done;
			const size_t l = rd_le16 (raw + p), end = p + 2 + l;
			const void *z = memchr (raw + p + 2, 0, l);
			if (!l || end > 0x18 + (size_t)pay || !z)
				goto done;
			p = end;
		}
		if (p != 0x18 + (size_t)pay)
			goto done;
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.d", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT VLIB:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, dest);
		err = ERR_OK;
		p = 0x18;
		for (u32 i = 0; i < cnt && !err && !testmode; i++)
		{
			const size_t l = rd_le16 (raw + p);
			ccp nm = (ccp)raw + p + 2;
			const size_t nl = strlen (nm);
			if (nm[0] == '/' || strstr (nm, "..") || strchr (nm, '\\') || !nl)
			{
				p += 2 + l;
				continue;
			}
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", nm);
			const size_t ds = l - nl - 1;
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && ds && fwrite (raw + p + 2 + nl + 1, 1, ds, F.f) != ds)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)ds, path);
			ResetFile (&F, opt_preserve);
			p += 2 + l;
		}
	}
done:
	FREE (raw);
	return err;
}
