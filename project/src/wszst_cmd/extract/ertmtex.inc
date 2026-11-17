// Telltale Games (Wii) "*.d3dtx" textures: "ERTM" metastream (version 4+) whose D3DTX body embeds a standard Wii TPL (magic 0x0020AF30, first 0x400 bytes of the file) that runs to the end of the file. The TPL is
// written to "<file>.tpl" and decoded to PNG by the existing TPL handler.
static enumError extract_ertmtex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".d3dtx") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x80 || memcmp (raw, "ERTM", 4))
		goto done;
	{
		size_t o = 0;
		for (size_t i = 8; i + 0x40 < sz && i < 0x400 && !o; i++)
			if (rd_be32 (raw + i) == 0x0020af30 && rd_be32 (raw + i + 4) == 1 && rd_be32 (raw + i + 8) == 0x0c && rd_be32 (raw + i + 12) + (u64)i < sz)
				o = i;
		if (!o)
			goto done;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s.tpl", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT D3DTX:%s -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, path);
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
