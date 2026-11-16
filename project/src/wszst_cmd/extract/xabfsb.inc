// Igor - The Game ".xab" sound banks ("chkf" chunk file: name table "xab\0header\0progdata\0wavedata\0...", then the parameters, with an FMOD "FSB4" bank embedded at an unaligned offset behind them).
// Every "FSB4" whose size (0x30 + sample header size + data size, little-endian fields) fits the file is carved to "<file>.d/NN.fsb" and extracted recursively.
static enumError extract_xabfsb_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".xab"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x100 || memcmp (raw, "chkf", 4))
		goto done;
	{
		uint found = 0;
		size_t at[16], len[16];
		for (size_t o = 0x20; o + 0x30 <= sz && found < 16; o++)
		{
			if (memcmp (raw + o, "FSB4", 4))
				continue;
			const u64 total = 0x30 + (u64)rd_le32 (raw + o + 8) + rd_le32 (raw + o + 12);
			if (rd_le32 (raw + o + 4) && o + total <= sz)
			{
				at[found] = o;
				len[found++] = (size_t)total;
				o += (size_t)total - 1;
			}
		}
		if (!found)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT XAB:%s (%u FSB banks) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, found, dest);
		err = ERR_OK;
		for (uint i = 0; i < found && !err && !testmode; i++)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%02u.fsb", dest, basedir ? basedir : "", i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (raw + at[i], 1, len[i], F.f) != len[i])
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)len[i], path);
			ResetFile (&F, opt_preserve);
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
