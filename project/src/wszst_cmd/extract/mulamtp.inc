// Angry Birds Trilogy (Wii) "Data/**/*.MTP" archives, little-endian: {"MULA", u32 count, count * {u32 size, u32 name offset}, u32 name pool size, NUL-terminated backslash paths (e.g.
// "DATA\CLASSIC\AUDIO\...\X.WAV"), then the members back to back, each padded to 4 bytes, starting at the 4-aligned end of the name pool}. Members are written under "<file>.d/<path>" and extracted recursively.
static enumError extract_mulamtp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".mtp"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x20 || memcmp (raw, "MULA", 4))
		goto done;
	{
		const u32 n = rd_le32 (raw + 4);
		if (!n || n > 100000 || 12 + 8 * (u64)n > sz)
			goto done;
		const size_t pool = 12 + 8 * (size_t)n;
		size_t pend = pool;
		for (u32 i = 0; i < n; i++)
		{
			const u32 no = rd_le32 (raw + 8 + 8 * (size_t)i + 4);
			if (pool + (u64)no >= sz)
				goto done;
			const size_t e = pool + no + strnlen ((ccp)raw + pool + no, sz - pool - no) + 1;
			if (e > pend)
				pend = e;
		}
		size_t p = (pend + 3) & ~(size_t)3;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MULA:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (u32 i = 0; i < n && !err; i++)
		{
			const u32 size = rd_le32 (raw + 8 + 8 * (size_t)i), no = rd_le32 (raw + 8 + 8 * (size_t)i + 4);
			if (p + size > sz)
				break;
			char name[PATH_MAX];
			snprintf (name, sizeof (name), "%s", (ccp)raw + pool + no);
			for (char *c = name; *c; c++)
				if (*c == '\\')
					*c = '/';
			ccp q = name;
			while (*q == '/')
				q++;
			if (*q && !strstr (q, "..") && !testmode)
			{
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", q);
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && size && fwrite (raw + p, 1, size, F.f) != size)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
				ResetFile (&F, opt_preserve);
			}
			p = (p + size + 3) & ~(size_t)3;
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
