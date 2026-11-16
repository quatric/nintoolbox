// "CARC" resource pack (Haneru no Tobira Wii and relatives, ".carc"), big-endian: {u32 magic 0x0a0b0e80, u32 count, u32 name table offset 0x10, u32 offset table position}, NUL-terminated
// member paths (e.g. "2d/main/title.arc") from 0x10, (padded with zeros), then count u32 absolute data offsets (ascending, 0x20-aligned; each member runs to the next offset, the last to the end of the file). Members
// (U8 archives, TPL, BRRES, ...) are written under their paths and extracted recursively.
static enumError extract_carcpack_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".carc"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	const u32 cnt = sz >= 0x20 ? rd_be32 (raw + 4) : 0, tab = sz >= 0x20 ? rd_be32 (raw + 12) : 0;
	if (sz < 0x20 || rd_be32 (raw) != 0x0a0b0e80 || !cnt || cnt > 0x10000 || rd_be32 (raw + 8) != 0x10 || tab <= 0x10 || tab + 4 * (u64)cnt > sz)
		goto done;
	{
		// names: cnt NUL-terminated strings, then zero padding up to the offset table
		size_t np = 0x10;
		for (u32 i = 0; i < cnt; i++)
		{
			const void *z = memchr (raw + np, 0, tab - np);
			if (!z || z == raw + np)
				goto done;
			np = (const u8 *)z - raw + 1;
		}
		for (; np < tab; np++)
			if (raw[np])
				goto done;
		u32 prev = 0;
		for (u32 i = 0; i < cnt; i++)
		{
			const u32 o = rd_be32 (raw + tab + 4 * (size_t)i);
			if (o < tab + 4 * cnt || o < prev || o > sz)
				goto done;
			prev = o;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT CARC:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, dest);
		err = ERR_OK;
		const char *nm = (ccp)raw + 0x10;
		for (u32 i = 0; i < cnt && !err && !testmode; i++, nm += strlen (nm) + 1)
		{
			const u32 o = rd_be32 (raw + tab + 4 * (size_t)i), e = i + 1 < cnt ? rd_be32 (raw + tab + 4 * (size_t)i + 4) : (u32)sz;
			if (nm[0] == '/' || strstr (nm, "..") || strchr (nm, '\\') || !nm[0])
				continue;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", nm);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && e > o && fwrite (raw + o, 1, e - o, F.f) != e - o)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e - o, path);
			ResetFile (&F, opt_preserve);
		}
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (raw);
	return err;
}
