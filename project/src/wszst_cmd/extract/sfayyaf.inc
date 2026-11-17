// Yuke's WWE SmackDown vs. Raw (Wii) "sound/*.yaf" sound banks, little-endian: {"SFAY", u32 0x10 (big-endian 0x10), char name[16], u32 0, u32 count, u32 alignment, u32 1, u32 entry size 12, u32 table offset 0x2c},
// 12-byte entries {u32 size, u32 offset, u32 id}; every member is a mono Nintendo DSP-ADPCM file. Members are carved to "<file>.d/NNNN_<id>.dsp" and decoded recursively to WAV.
static enumError extract_sfayyaf_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".yaf"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || memcmp (raw, "SFAY", 4))
		goto done;
	{
		const u32 n = rd_le32 (raw + 0x18), stride = rd_le32 (raw + 0x24), tab = rd_le32 (raw + 0x28);
		if (!n || n > 100000 || stride != 12 || tab < 0x2c || tab + (u64)n * 12 > sz)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT YAF:%s (%u sounds) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = raw + tab + 12 * i;
			const u64 size = rd_le32 (e), off = rd_le32 (e + 4);
			if (off + size > sz || size < 0x60)
				continue;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%04u_%u.dsp", dest, basedir ? basedir : "", i, rd_le32 (e + 8));
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (raw + off, 1, size, F.f) != size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)size, path);
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
