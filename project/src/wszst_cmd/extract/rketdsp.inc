// Aqua Panic! (Wii) "common/*.eng|*.fre" sound banks: "RKET" container whose members are complete Nintendo DSP-ADPCM files (0x60-byte big-endian DSP header immediately followed by the
// sample data, 2-byte aligned, no usable index). The members are located by scanning for consistent DSP headers (start address 2, current address 2, end address = nibbles - 1,
// plausible rate), carved to "<file>.d/NNNN.dsp" and decoded recursively to WAV. The LangLvl "*.eng" level files carry the same magic but hold no DSP data and are left alone.
static enumError extract_rketdsp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".eng") && !is_ext (arg, ".fre") && !is_ext (arg, ".ger") && !is_ext (arg, ".spa") && !is_ext (arg, ".ita") && !is_ext (arg, ".dut"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x100 || memcmp (raw, "RKET", 4))
		goto done;
	{
		uint n = 0;
		u64 offs[4096], lens[4096];
		for (u64 o = 0x40; o + 0x60 < sz && n < 4096;)
		{
			const u8 *h = raw + o;
			const u32 ns = rd_be32 (h), nib = rd_be32 (h + 4), rate = rd_be32 (h + 8);
			if (nib >= 16 && nib < 0x10000000 && rate >= 4000 && rate <= 48000 && rd_be16 (h + 12) <= 1 && !rd_be16 (h + 14) && rd_be32 (h + 0x10) == 2
				&& rd_be32 (h + 0x14) == nib - 1 && rd_be32 (h + 0x18) == 2 && ns && ns <= nib / 16 * 14 + 14 && o + 0x60 + (nib + 1) / 2 <= sz)
			{
				offs[n] = o;
				lens[n++] = 0x60 + (nib + 1) / 2;
				o += 0x60 + (nib + 1) / 2;
			}
			else
				o += 2;
		}
		if (!n)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT RKET:%s (%u sounds) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%04u.dsp", dest, basedir ? basedir : "", i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (raw + offs[i], 1, lens[i], F.f) != lens[i])
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)lens[i], path);
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
