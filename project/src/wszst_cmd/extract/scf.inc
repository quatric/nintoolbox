// Roogoo - Twisted Towers "DataFiles.scf": {u8 1} then repeated {char name[] NUL, le32 compressed size, zlib stream}; the stream ends exactly at the end of the file. Members are inflated to "<file>.d/<name>".
static enumError extract_scf_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".scf") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 16 || raw[0] != 1)
		goto done;
	{
		uint n = 0;
		size_t p = 1;
		while (p < sz)
		{
			const u8 *e = memchr (raw + p, 0, sz - p);
			if (!e || e == raw + p || e - (raw + p) > 200 || (size_t)(e - raw) + 5 > sz)
				goto done;
			const size_t cs = rd_le32 (e + 1), zs = (size_t)(e - raw) + 5;
			if (zs + cs > sz || cs < 6 || raw[zs] != 0x78)
				goto done;
			p = zs + cs;
			n++;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SCF:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		p = 1;
		while (p < sz && !err && !testmode)
		{
			const u8 *e = memchr (raw + p, 0, sz - p);
			char name[256];
			uint k = 0;
			for (; raw + p + k < e && k < 200; k++)
				name[k] = raw[p + k] == '/' || raw[p + k] == '\\' || raw[p + k] < 0x20 ? '_' : raw[p + k];
			name[k] = 0;
			if (!strcmp (name, ".") || !strcmp (name, ".."))
				name[0] = '_';
			const size_t cs = rd_le32 (e + 1), zs = (size_t)(e - raw) + 5;
			p = zs + cs;
			uLongf cap = 1 << 16;
			u8 *out = 0;
			int zr;
			for (;;)
			{
				FREE (out);
				out = MALLOC (cap);
				uLongf dl = cap;
				zr = uncompress (out, &dl, raw + zs, (uLong)cs);
				if (zr == Z_BUF_ERROR && cap < (1u << 28))
				{
					cap *= 4;
					continue;
				}
				cap = dl;
				break;
			}
			if (zr == Z_OK)
			{
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%s", dest, name);
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && cap && fwrite (out, 1, cap, F.f) != cap)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %lu bytes failed: %s\n", (unsigned long)cap, path);
				ResetFile (&F, opt_preserve);
			}
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
