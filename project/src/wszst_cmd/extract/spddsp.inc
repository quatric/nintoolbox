// Rockstar Games Presents Table Tennis (Wii) "zaudio/WiiBanks/*.spd": headerless DSP-ADPCM sample data with the table "<name>.spt" next to it, big-endian: {u32 count, count * 28-byte headers {u32 0, u32 rate, u32 0,
// u32 0, u32 end nibble address, u32 start nibble address, u32 0}, count * 46-byte blocks {s16 coefficients[16], u16 gain, ps, hist1, hist2, loop ps, loop hist1, loop hist2}}. Addresses are absolute nibble addresses
// in the ".spd" (the frame header of a sound sits 2 nibbles before its start address). Every sound is wrapped into a standard DSP file "<file>.d/NNNN.dsp" and decoded recursively to WAV.
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
	const u32 n = tsz >= 4 ? rd_be32 (tab) : 0;
	if (!n || n > 10000 || tsz != 4 + 74 * (size_t)n)
		goto done;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		goto done;
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SPD:%s (%u sounds) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		uint made = 0;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *h = tab + 4 + 28 * (size_t)i, *cb = tab + 4 + 28 * (size_t)n + 46 * (size_t)i;
			const u32 rate = rd_be32 (h + 4), end = rd_be32 (h + 16), start = rd_be32 (h + 20);
			bool coef = false;
			for (uint k = 0; k < 32; k++)
				coef = coef || cb[k];
			if (!coef || rate < 4000 || rate > 96000 || start < 2 || (start & 1) || end < start)
				continue;
			const u64 first = (u64)(start - 2) / 2, nib = (u64)end - (start - 2) + 1, bytes = (nib + 1) / 2;
			if (first + bytes > sz)
				continue;
			const u64 padded = (bytes + 7) & ~(u64)7; // a cut-short last frame is zero-padded to a whole 8-byte frame
			u8 *d = CALLOC (1, 0x60 + padded);
			if (!d)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			#define W32(o, v) (d[o] = (v) >> 24, d[(o) + 1] = (v) >> 16, d[(o) + 2] = (v) >> 8, d[(o) + 3] = (v))
			W32 (0, (u32)(nib / 16 * 14 + (nib % 16 > 2 ? nib % 16 - 2 : 0)));
			W32 (4, (u32)nib);
			W32 (8, rate);
			W32 (0x10, 2);
			W32 (0x14, (u32)nib - 1);
			W32 (0x18, 2);
			memcpy (d + 0x1c, cb, 32);
			memcpy (d + 0x3c, cb + 32, 14);
			#undef W32
			memcpy (d + 0x60, raw + first, (size_t)bytes);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%04u.dsp", dest, basedir ? basedir : "", i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (d, 1, 0x60 + padded, F.f) != 0x60 + padded)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)(0x60 + padded), path);
			ResetFile (&F, opt_preserve);
			FREE (d);
			made++;
		}
		if (!err && !testmode && made)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (raw);
	FREE (tab);
	return err;
}
