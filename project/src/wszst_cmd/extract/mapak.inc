// Rubik's Puzzle World (Wii) ".ma" archives, little-endian: u32 count, count * {u16 name length (with NUL), path, u32 offset, u32 size}, the members stored back to back
// at their offsets (16 byte aligned). Members are written below "<file>.d/" under their stored path and extracted recursively.
static enumError extract_mapak_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".ma"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x20)
		goto done;
	{
		const u32 n = rd_le32 (raw);
		if (!n || n > 100000)
			goto done;
		size_t p = 4, first = sz;
		for (u32 i = 0; i < n; i++)
		{
			if (p + 2 > sz)
				goto done;
			const uint l = rd_le16 (raw + p);
			if (l < 2 || p + 2 + l + 8 > sz || raw[p + 1 + l] != 0)
				goto done;
			for (uint k = 0; k + 1 < l; k++)
				if (raw[p + 2 + k] < 0x20 || raw[p + 2 + k] > 0x7e)
					goto done;
			const u64 o = rd_le32 (raw + p + 2 + l), s = rd_le32 (raw + p + 6 + l);
			if (o + s > sz || o < p + 2 + l + 8)
				goto done;
			if (o < first)
				first = o;
			p += 2 + l + 8;
		}
		if (p > first)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MA:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		p = 4;
		for (u32 i = 0; i < n && !err && !testmode; i++)
		{
			const uint l = rd_le16 (raw + p);
			const u32 o = rd_le32 (raw + p + 2 + l), s = rd_le32 (raw + p + 6 + l);
			char nm[0x200];
			snprintf (nm, sizeof (nm), "%.*s", (int)(l - 1), (ccp)raw + p + 2);
			p += 2 + l + 8;
			if (strstr (nm, "..") || nm[0] == '/')
				continue;
			err = shade_write (arg, dest, basedir, nm, raw + o, s);
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
