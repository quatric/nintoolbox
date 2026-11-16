// Agatha Christie: And Then There Were None (Wii) ".wii" data bundles (dialogue.wii, ...), little-endian: u32 count, 0x1c zero bytes, count * 0x100 byte records
// {char path[0xf4] (backslash separated, 0xcd padded), u32 offset, u32 size, u32 size used}, then the files back to back from 0x20 + count * 0x100. Members are written
// below "<file>.d/" under their stored path (backslashes become directories).
static enumError extract_wiidir_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".wii"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x120)
		goto done;
	{
		const u32 n = rd_le32 (raw);
		if (!n || n > 100000 || 0x20 + 0x100 * (u64)n > sz)
			goto done;
		for (u32 i = 0; i < n; i++)
		{
			const u8 *r = raw + 0x20 + 0x100 * (size_t)i;
			const u64 o = rd_le32 (r + 0xf4), l = rd_le32 (r + 0xf8);
			if (o < 0x20 + 0x100 * (u64)n || o + l > sz || !r[0] || r[0] < 0x20 || r[0] > 0x7e || memchr (r, 0, 0xf4) == 0)
				goto done;
			for (const u8 *p = r; *p; p++)
				if (*p < 0x20 || *p > 0x7e)
					goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT WII-DIR:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (u32 i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *r = raw + 0x20 + 0x100 * (size_t)i;
			char nm[0x100];
			snprintf (nm, sizeof (nm), "%s", (ccp)r);
			for (char *p = nm; *p; p++)
				if (*p == '\\')
					*p = '/';
			if (strstr (nm, "..") || nm[0] == '/')
				continue;
			err = shade_write (arg, dest, basedir, nm, raw + rd_le32 (r + 0xf4), rd_le32 (r + 0xf8));
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
