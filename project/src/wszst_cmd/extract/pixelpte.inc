// Pixel Ent. (Barbie Horse Adventures) texture exports "*.pte" / "*.pfi": a big-endian header {u32 13, u32 type, name, source path ... .tga} followed by a plain Wii TPL (magic 0x0020AF30, table offset 0xc) that runs to the end of the file.
// The TPL is written to "<file>.tpl" and decoded to PNG by the existing TPL handler.
static enumError extract_pixelpte_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".pte") && !is_ext (arg, ".pfi"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x100 || rd_be32 (raw) != 13)
		goto done;
	{
		size_t o = 0;
		for (size_t i = 0x20; i + 0x40 < sz && i < 0x400 && !o; i++)
			if (rd_be32 (raw + i) == 0x0020af30 && rd_be32 (raw + i + 4) == 1 && rd_be32 (raw + i + 8) == 0x0c)
				o = i;
		if (!o)
			goto done;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s.tpl", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT PTE:%s -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, path);
		err = ERR_OK;
		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (raw + o, 1, sz - o, F.f) != sz - o)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", sz - o, path);
			ResetFile (&F, opt_preserve);
			if (!err)
			{
				const enumError sub = extract_one_file (path, 0, depth + 1);
				if (sub != ERR_NOTHING_TO_DO && err < sub)
					err = sub;
			}
		}
	}
done:
	FREE (raw);
	return err;
}
