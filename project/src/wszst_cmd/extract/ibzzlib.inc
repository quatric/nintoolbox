// Jimmie Johnson's Anything with an Engine (Wii) "*.ibz" zlib files: {u32 LE uncompressed size} followed by one zlib stream. The inflated data (e.g. "ISOP" configuration data) is written to
// "<file>.d/<name>.bin" and extracted recursively.
static enumError extract_ibzzlib_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".ibz"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 8 || raw[4] != 0x78)
		goto done;
	{
		uint osz = rd_le32 (raw);
		size_t used = 0;
		u8 *out = osz && osz < 0x10000000 ? fst_inflate (raw + 4, sz - 4, &osz, &used) : 0;
		if (!out)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT IBZ:%s (%u bytes) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, osz, dest);
		err = ERR_OK;
		if (!testmode)
		{
			ccp b = strrchr (arg, '/');
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s.bin", dest, basedir ? basedir : "", b ? b + 1 : arg);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (out, 1, osz, F.f) != osz)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", osz, path);
			ResetFile (&F, opt_preserve);
			if (!err)
			{
				const enumError sub = extract_tree_complete (dest, depth + 1);
				if (err < sub)
					err = sub;
			}
		}
		FREE (out);
	}
done:
	FREE (raw);
	return err;
}
