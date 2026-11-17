// Argos no Senshi - Muscle Impact (Wii) "data/tex/*.tgs" texture containers: 0x40-byte header {"tgs\0", 0, u32 3, 0, u32 file size, u32 0x20, u32 0x20, 0, u32 TPL offset (0x40), u32 TPL end, u32 tail end, ...}
// followed by a standard Wii TPL (magic 0x0020AF30) and a short tail. The TPL is written to "<file>.tpl" and decoded to PNG by the existing TPL handler.
static enumError extract_tgstex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".tgs") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x60 || memcmp (raw, "tgs\0", 4) || rd_be32 (raw + 0x10) != sz)
		goto done;
	{
		const u32 off = rd_be32 (raw + 0x20), end = rd_be32 (raw + 0x24);
		if (off < 0x40 || off >= end || end > sz || end - off < 0x20 || rd_be32 (raw + off) != 0x0020af30)
			goto done;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s.tpl", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT TGS:%s -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, path);
		err = ERR_OK;
		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (raw + off, 1, end - off, F.f) != end - off)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", end - off, path);
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
