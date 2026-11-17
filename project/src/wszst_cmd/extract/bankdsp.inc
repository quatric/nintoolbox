// Backyard Sports (Wii) audio ".bank" sound banks, big-endian: {u32 last index, u32 0x20, u32 sample rate, u32 1, u32 count, count * {u32 offset, u32 size}}; every member is a complete Nintendo DSP-ADPCM file
// (0x60-byte header + data) at its offset. Written as "<file>.d/NNNN.dsp" and decoded recursively to WAV.
static enumError extract_bankdsp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bank"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	const u32 n = sz >= 0x20 ? rd_be32 (raw + 0x10) : 0;
	if (sz < 0x100 || rd_be32 (raw + 4) != 0x20 || !n || n > 4096 || 0x14 + 8 * (u64)n > sz)
		goto done;
	for (u32 i = 0; i < n; i++)
	{
		const u32 o = rd_be32 (raw + 0x14 + 8 * (size_t)i), s = rd_be32 (raw + 0x18 + 8 * (size_t)i);
		if (o < 0x14 + 8 * n || (u64)o + s > sz || s < 0x60 || !IsDSP (raw + o, s))
			goto done;
	}
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT BANK:%s (%u sounds) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (u32 i = 0; i < n && !err && !testmode; i++)
		{
			const u32 o = rd_be32 (raw + 0x14 + 8 * (size_t)i), s = rd_be32 (raw + 0x18 + 8 * (size_t)i);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%04u.dsp", dest, basedir ? basedir : "", i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (raw + o, 1, s, F.f) != s)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
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
