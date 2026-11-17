// Rockstar Games (Bully: Scholarship Edition, Wii) "audio/WII/*.SEA" sound banks: {u32 BE offset of the embedded FMOD bank (0xc0 ...), sound descriptions with CDCD-padded names ...} and an "FSB3"/"FSB4" bank
// at that offset running to the end of the file. The bank is carved to "<file>.d/bank.fsb" and extracted recursively.
static enumError extract_seafsb_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".sea"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	const u32 off = sz >= 0x40 ? rd_be32 (raw) : 0;
	if (off < 0x20 || off + 0x30 > sz || (memcmp (raw + off, "FSB3", 4) && memcmp (raw + off, "FSB4", 4)))
		goto done;
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SEA:%s (FSB bank at 0x%x) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, off, dest);
		err = ERR_OK;
		if (!testmode)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%sbank.fsb", dest, basedir ? basedir : "");
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (raw + off, 1, sz - off, F.f) != sz - off)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)(sz - off), path);
			ResetFile (&F, opt_preserve);
			if (!err)
			{
				const enumError sub = extract_tree_complete (dest, depth + 1);
				if (err < sub)
					err = sub;
			}
		}
	}
done:
	FREE (raw);
	return err;
}
