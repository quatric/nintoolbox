// Rampage: Total Destruction (Wii) "Audio/*.uber" + "*.samp" sound bank. The .uber is big-endian: "REBU" {u32 0x00010001, u32 section offsets x6}; the "RIDS" (sample directory) section holds
// {"RIDS", u32 0x00010001, u32 .samp size, u32 count, 16 bytes} followed by 0x40-byte entries {u32 start+2, u32 byte length, u32 0, u32 rate, s16 DSP-ADPCM coefficients[16], u32 flags, pad}; entry 0 is
// empty (all 0xff). The sample data is raw DSP-ADPCM in the sibling ".samp"; each sample is wrapped into a standard 0x60-byte DSP header ("<file>.d/NNNN.dsp") and decoded recursively to WAV.
static enumError extract_uber_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".uber"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *smp = 0;
	size_t sz = 0, ssz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x100 || memcmp (raw, "REBU", 4) || rd_be32 (raw + 4) != 0x10001)
		goto done;
	{
		char sp[PATH_MAX];
		snprintf (sp, sizeof (sp), "%s", arg);
		char *dot = strrchr (sp, '.');
		if (!dot)
			goto done;
		strcpy (dot, ".samp");
		if (LoadFileAlloc (sp, 0, 0, &smp, &ssz, 0, 0, 0, false) || !smp)
			goto done;
		const u32 dir = rd_be32 (raw + 0x10);
		if (dir + 0x20 > sz || memcmp (raw + dir, "RIDS", 4))
			goto done;
		const u32 count = rd_be32 (raw + dir + 0xc);
		if (!count || (u64)dir + 0x10 + (u64)count * 0x40 > sz)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		uint n = 0;
		for (uint i = 1; i < count; i++)
		{
			const u8 *e = raw + dir + 0x10 + i * 0x40;
			const u32 a = rd_be32 (e), len = rd_be32 (e + 4), rate = rd_be32 (e + 12);
			if (a < 2 || !len || rate < 4000 || rate > 96000 || (u64)a - 2 + len > ssz)
				continue;
			n++;
		}
		if (!n)
			goto done;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT UBER:%s (%u samples) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 1; i < count && !err && !testmode; i++)
		{
			const u8 *e = raw + dir + 0x10 + i * 0x40;
			const u32 a = rd_be32 (e), len = rd_be32 (e + 4), rate = rd_be32 (e + 12);
			if (a < 2 || !len || rate < 4000 || rate > 96000 || (u64)a - 2 + len > ssz)
				continue;
			const u32 nib = len * 2, ns = nib / 16 * 14 + (nib % 16 > 2 ? nib % 16 - 2 : 0);
			u8 h[0x60];
			memset (h, 0, sizeof (h));
			wr_be32 (h, ns);
			wr_be32 (h + 4, nib);
			wr_be32 (h + 8, rate);
			wr_be32 (h + 0x10, 2);
			wr_be32 (h + 0x14, nib - 1);
			wr_be32 (h + 0x18, 2);
			memcpy (h + 0x1c, e + 16, 32);
			h[0x3f] = smp[a - 2];
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%04u.dsp", dest, basedir ? basedir : "", i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && (fwrite (h, 1, sizeof (h), F.f) != sizeof (h) || fwrite (smp + a - 2, 1, len, F.f) != len))
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", len, path);
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
	FREE (smp);
	return err;
}
