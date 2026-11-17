// Disney Think Fast ".MIB" music: Nintendo DSP-ADPCM channel files stored back to back, every channel with its own 0x60-byte DSP header (sample count, nibble count, rate, ... identical in all channels) at a
// 0x800-aligned offset (0 and 0x220000 in BT.MIB). The channels are carved to "<file>.d/chN.dsp" and decoded recursively to WAV.
static enumError extract_mibdsp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".mib"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x1000 || !IsDSP (raw, sz))
		goto done;
	{
		uint n = 1;
		size_t off[8] = { 0 };
		for (size_t o = 0x800; o + 0x60 <= sz && n < 8; o += 0x800)
			if (!memcmp (raw + o, raw, 12) && IsDSP (raw + o, sz - o))
				off[n++] = o;
		const size_t ch0 = n > 1 ? off[1] : sz;
		for (uint i = 1; i < n; i++)
			if (off[i] != i * ch0)
				goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MIB:%s (%u channels) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const size_t s = i + 1 < n ? off[i + 1] - off[i] : sz - off[i];
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%sch%u.dsp", dest, basedir ? basedir : "", i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (raw + off[i], 1, s, F.f) != s)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)s, path);
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
