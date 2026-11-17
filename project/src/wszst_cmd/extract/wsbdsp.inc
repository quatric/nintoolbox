// Star Trek: Conquest (Wii) "Audio/**/*.WSB" sound banks with the headerless DSP-ADPCM data file "<name>.WSS" next to them. WSB is big-endian: {u32 1, u32 0xf4 (header size), u32 count, u32 count, name at 0x10, ...}, a table
// of 0x14-byte parameter entries, and at the end of the file (count * 0x40 + 4 bytes from the end) the sound records {u32 offset into the WSS, u32 nibbles, u32 2 (start address), u32 rate, s16 coefficients[16],
// u32 initial ps, u32 hist1, u32 hist2, u32 ...}. Every sound is wrapped into a standard DSP file "<file>.d/NNNN.dsp" and decoded recursively to WAV.
static enumError extract_wsbdsp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".wsb"))
		return ERR_NOTHING_TO_DO;
	char wss[PATH_MAX];
	snprintf (wss, sizeof (wss), "%s", arg);
	const size_t al = strlen (wss);
	if (al < 4)
		return ERR_NOTHING_TO_DO;
	u8 *tab = 0, *raw = 0;
	size_t tsz = 0, sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &tab, &tsz, 0, 0, 0, false) || !tab)
		goto done;
	const u32 n = tsz >= 0x100 ? rd_be32 (tab + 8) : 0;
	if (!n || n > 10000 || rd_be32 (tab) != 1 || rd_be32 (tab + 12) != n || tsz < 0xf4 + 4 + (size_t)n * 0x40)
		goto done;
	memcpy (wss + al - 3, "WSS", 3);
	if (LoadFileAlloc (wss, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		memcpy (wss + al - 3, "wss", 3);
		if (LoadFileAlloc (wss, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
			goto done;
	}
	{
		const size_t base = tsz - (size_t)n * 0x40 - 4;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT WSB:%s (%u sounds) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		uint made = 0;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *r = tab + base + 0x40 * (size_t)i;
			const u32 off = rd_be32 (r), nib = rd_be32 (r + 4), rate = rd_be32 (r + 12), ps = rd_be32 (r + 48);
			bool coef = false;
			for (uint k = 0; k < 32; k++)
				coef = coef || r[16 + k];
			const u64 bytes = ((u64)nib + 1) / 2;
			if (!coef || !nib || rate < 4000 || rate > 96000 || (u64)off + bytes > sz)
				continue;
			u8 *d = CALLOC (1, 0x60 + bytes);
			if (!d)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			#define W32(o, v) (d[o] = (v) >> 24, d[(o) + 1] = (v) >> 16, d[(o) + 2] = (v) >> 8, d[(o) + 3] = (v))
			W32 (0, (u32)(nib / 16 * 14 + (nib % 16 > 2 ? nib % 16 - 2 : 0)));
			W32 (4, nib);
			W32 (8, rate);
			W32 (0x10, 2);
			W32 (0x14, nib - 1);
			W32 (0x18, 2);
			memcpy (d + 0x1c, r + 16, 32);
			d[0x3f] = (u8)ps;
			#undef W32
			memcpy (d + 0x60, raw + off, (size_t)bytes);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%04u.dsp", dest, basedir ? basedir : "", i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (d, 1, 0x60 + bytes, F.f) != 0x60 + bytes)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)(0x60 + bytes), path);
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
