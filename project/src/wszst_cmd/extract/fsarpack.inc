// Freestyle Games "FSAR" archives (DJ Hero, Wii "FAR/*.FAR"), big-endian: {"FSAR", u32 version (2), u32 offset of the file data, u32 entry count, 16 bytes padding}, then count * 0x120-byte entries {char
// path[0x100] (backslash separated, NUL padded), u32 0, u32 unpacked size, u32 0, u32 packed size, u32 0, u32 data offset (relative to the file data), u32 method (2 = zlib), u32 0}. Members are written below
// "<file>.d/" with their original relative paths ("\" -> "/") and extracted recursively.
static enumError extract_fsarpack_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".far"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x140 || memcmp (raw, "FSAR", 4) || rd_be32 (raw + 4) != 2)
		goto done;
	{
		const u32 base = rd_be32 (raw + 8), n = rd_be32 (raw + 12);
		if (!n || n > 100000 || 0x20 + 0x120 * (u64)n != base || base > sz)
			goto done;
		for (u32 i = 0; i < n; i++)
		{
			const u8 *e = raw + 0x20 + 0x120 * (size_t)i;
			const u32 us = rd_be32 (e + 0x104), cs = rd_be32 (e + 0x10c), off = rd_be32 (e + 0x114), meth = rd_be32 (e + 0x118);
			if (!memchr (e, 0, 0x100) || (meth != 2 && meth != 0) || (u64)base + off + cs > sz || (!meth && us != cs))
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT FSAR:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (u32 i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = raw + 0x20 + 0x120 * (size_t)i;
			const u32 us = rd_be32 (e + 0x104), cs = rd_be32 (e + 0x10c), off = rd_be32 (e + 0x114), meth = rd_be32 (e + 0x118);
			char name[0x101];
			memcpy (name, e, 0x100);
			name[0x100] = 0;
			char *q = name;
			for (char *c = name; *c; c++)
				if (*c == '\\')
					*c = '/';
			while (*q == '/')
				q++;
			if (!*q || strstr (q, ".."))
				continue;
			u8 *out = 0;
			const u8 *data = raw + base + off;
			uint osz = us;
			if (meth)
			{
				size_t used = 0;
				out = fst_inflate (data, cs, &osz, &used);
				if (!out)
					continue;
				data = out;
			}
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", q);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && osz && fwrite (data, 1, osz, F.f) != osz)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", osz, path);
			ResetFile (&F, opt_preserve);
			FREE (out);
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
