// Rockstar Games Presents Table Tennis (Wii) "zaudio/WiiBanks/*.spd": headerless DSP-ADPCM sample data with the table "<name>.spt" next to it. Only the single-sound layout (the 78-byte table of the music banks) is
// handled: {u32 1, u32 0, u32 sample rate, u32 0, u32 0, u32 end nibble address, u32 start address (2), u32 0, s16 coefficients[16], 14 bytes gain / history, ...}. The sample data is wrapped into a standard DSP file
// "<file>.d/sound.dsp" and decoded recursively to WAV. Multi-sound and stereo banks use a different table and are skipped.
static enumError extract_spddsp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".spd"))
		return ERR_NOTHING_TO_DO;
	char spt[PATH_MAX];
	snprintf (spt, sizeof (spt), "%s", arg);
	const size_t al = strlen (spt);
	if (al < 4)
		return ERR_NOTHING_TO_DO;
	memcpy (spt + al - 3, "spt", 3);
	u8 *raw = 0, *tab = 0;
	size_t sz = 0, tsz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (spt, 0, 0, &tab, &tsz, 0, 0, 0, false) || !tab)
	{
		memcpy (spt + al - 3, "SPT", 3);
		if (LoadFileAlloc (spt, 0, 0, &tab, &tsz, 0, 0, 0, false) || !tab)
			goto done;
	}
	if (tsz != 78 || rd_be32 (tab) != 1)
		goto done;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		goto done;
	{
		const u32 rate = rd_be32 (tab + 8), end = rd_be32 (tab + 0x14), start = rd_be32 (tab + 0x18);
		const u8 *co = tab + 0x20;
		bool coef = false;
		for (uint i = 0; i < 32; i++)
			coef = coef || co[i];
		const u64 nib = (u64)end + 1, bytes = (nib + 1) / 2;
		if (!coef || rate < 4000 || rate > 96000 || start != 2 || !end || bytes > sz + 8)
			goto done;
		const size_t dl = bytes > sz ? sz : (size_t)bytes;
		u8 *d = CALLOC (1, 0x60 + dl);
		if (!d)
			goto done;
		#define W32(o, v) (d[o] = (v) >> 24, d[(o) + 1] = (v) >> 16, d[(o) + 2] = (v) >> 8, d[(o) + 3] = (v))
		W32 (0, (u32)(nib / 16 * 14 + (nib % 16 > 2 ? nib % 16 - 2 : 0)));
		W32 (4, (u32)nib);
		W32 (8, rate);
		W32 (0x10, 2);
		W32 (0x14, end);
		W32 (0x18, 2);
		memcpy (d + 0x1c, co, 32);
		d[0x3f] = raw[0];
		#undef W32
		memcpy (d + 0x60, raw, dl);
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SPD:%s (1 sound, %u Hz) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, rate, dest);
		err = ERR_OK;
		if (!testmode)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%ssound.dsp", dest, basedir ? basedir : "");
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (d, 1, 0x60 + dl, F.f) != 0x60 + dl)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)(0x60 + dl), path);
			ResetFile (&F, opt_preserve);
			if (!err)
			{
				const enumError sub = extract_tree_complete (dest, depth + 1);
				if (err < sub)
					err = sub;
			}
		}
		FREE (d);
	}
done:
	FREE (raw);
	FREE (tab);
	return err;
}
