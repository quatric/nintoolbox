// Baroque (Wii) "SOUND_xx/brqsnd.snt" + "brqsnd.snd" sound bank: the big-endian ".snt" table is {u32 count, u32 0, u32 0} followed by 0x14-byte entries {u32 type, u32 volume (100), u32 kind, u32 flag,
// u32 END offset into the .snd} (member i spans [end(i-1), end(i)); the first starts at 0). Most members up to the tail are standalone mono DSP-ADPCM files (0x60-byte header + data); the tail only holds 16-byte
// names of external SOUND_xx/*.dsp files and is skipped. Valid members are written to "<file>.d/NNNN.dsp" and decoded to WAV.
static enumError extract_sntbank_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".snt"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *snd = 0;
	size_t sz = 0, ssz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x20)
		goto done;
	{
		u32 count = rd_be32 (raw);
		if (!count || count > 100000 || rd_be32 (raw + 4) || rd_be32 (raw + 8))
			goto done;
		if (0x0c + (u64)count * 0x14 > sz) // the stored count may include one entry more than the table holds
			count = (u32)((sz - 0x0c) / 0x14);
		char sp[PATH_MAX];
		snprintf (sp, sizeof (sp), "%s", arg);
		char *dot = strrchr (sp, '.');
		if (!dot)
			goto done;
		strcpy (dot, ".snd");
		if (LoadFileAlloc (sp, 0, 0, &snd, &ssz, 0, 0, 0, false) || !snd)
			goto done;
		uint n = 0;
		u64 prev = 0;
		for (uint i = 0; i < count; i++)
		{
			const u64 end = rd_be32 (raw + 0x0c + (size_t)i * 0x14 + 16);
			if (end >= prev + 0x60 && end <= ssz)
			{
				const u8 *m = snd + prev;
				if (rd_be32 (m + 0x10) == 2 && rd_be32 (m + 0x18) == 2 && rd_be32 (m + 4) >= 16 && rd_be32 (m + 0x14) == rd_be32 (m + 4) - 1 && rd_be32 (m + 8) >= 3000 && rd_be32 (m + 8) <= 96000
					&& prev + 0x60 + (rd_be32 (m + 4) + 1) / 2 <= end)
					n++;
			}
			prev = end > prev ? end : prev;
		}
		if (!n)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SNT:%s (%u sounds) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		prev = 0;
		for (uint i = 0; i < count && !err; i++)
		{
			const u64 end = rd_be32 (raw + 0x0c + (size_t)i * 0x14 + 16);
			if (end >= prev + 0x60 && end <= ssz)
			{
				const u8 *m = snd + prev;
				if (rd_be32 (m + 0x10) == 2 && rd_be32 (m + 0x18) == 2 && rd_be32 (m + 4) >= 16 && rd_be32 (m + 0x14) == rd_be32 (m + 4) - 1 && rd_be32 (m + 8) >= 3000 && rd_be32 (m + 8) <= 96000
					&& prev + 0x60 + (rd_be32 (m + 4) + 1) / 2 <= end && !testmode)
				{
					const size_t len = 0x60 + (rd_be32 (m + 4) + 1) / 2;
					char path[PATH_MAX];
					snprintf (path, sizeof (path), "%s/%s%04u.dsp", dest, basedir ? basedir : "", i);
					File_t F;
					err = CreateFileOpt (&F, true, path, false, arg);
					if (F.f && fwrite (m, 1, len, F.f) != len)
						err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)len, path);
					ResetFile (&F, opt_preserve);
				}
			}
			prev = end > prev ? end : prev;
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
	FREE (snd);
	return err;
}
