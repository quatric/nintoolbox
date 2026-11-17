// Destroy All Humans! Big Willy Unleashed (Wii) "AUDIO/VBK/<lang>/*.vbk" sound banks with the index file of the same name and the extension ".vbi": big-endian 16-byte index entries {u32 offset in the bank, f32 sample rate,
// f32 volume, u32 1}. Every sound is {u32 2, u32 nibbles, u32 nibbles, u32 2, s16 coefs[16], u16 gain, ps, hist1, hist2, loop ps, loop hist1, hist2, ...} with the DSP-ADPCM frames from +0x40. Each sound is wrapped
// into a standard DSP file "<file>.d/NNNN.dsp" and decoded recursively to WAV.
static enumError extract_vbkdsp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".vbk"))
		return ERR_NOTHING_TO_DO;
	char idx[PATH_MAX];
	snprintf (idx, sizeof (idx), "%s", arg);
	const size_t il = strlen (idx);
	if (il < 4)
		return ERR_NOTHING_TO_DO;
	memcpy (idx + il - 3, "vbi", 3);
	u8 *raw = 0, *tab = 0;
	size_t sz = 0, tsz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		goto done;
	if (LoadFileAlloc (idx, 0, 0, &tab, &tsz, 0, 0, 0, false) || !tab)
	{
		memcpy (idx + il - 3, "VBI", 3);
		if (LoadFileAlloc (idx, 0, 0, &tab, &tsz, 0, 0, 0, false) || !tab)
			goto done;
	}
	{
		const uint n = tsz / 16;
		if (!n || n > 100000 || tsz % 16 || sz < 0x40)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT VBK:%s (%u sounds) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u64 off = rd_be32 (tab + 16 * i), end = i + 1 < n ? rd_be32 (tab + 16 * i + 16) : sz;
			if (off + 0x40 > end || end > sz || rd_be32 (raw + off) != 2)
				continue;
			const u8 *s = raw + off;
			const u32 nib = rd_be32 (s + 4);
			const u32 rate = (u32)(*(const float *)(u32[]){ rd_be32 (tab + 16 * i + 4) } + 0.5f);
			const u64 bytes = (nib + 1) / 2;
			if (!nib || off + 0x40 + bytes > end || !rate)
				continue;
			const u32 samples = nib / 16 * 14 + (nib % 16 > 2 ? nib % 16 - 2 : 0);
			const size_t total = 0x60 + bytes;
			u8 *d = CALLOC (1, total);
			if (!d)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			#define W32(o, v) (d[o] = (v) >> 24, d[(o) + 1] = (v) >> 16, d[(o) + 2] = (v) >> 8, d[(o) + 3] = (v))
			W32 (0, samples);
			W32 (4, nib);
			W32 (8, rate);
			W32 (0x10, 2);
			W32 (0x14, nib + 1);
			W32 (0x18, 2);
			memcpy (d + 0x1c, s + 0x10, 32);
			memcpy (d + 0x3c, s + 0x30, 4); // gain, initial predictor/scale
			memcpy (d + 0x40, s + 0x34, 4); // history samples
			memcpy (d + 0x60, s + 0x40, bytes);
			#undef W32
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%04u.dsp", dest, basedir ? basedir : "", i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (d, 1, total, F.f) != total)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)total, path);
			ResetFile (&F, opt_preserve);
			FREE (d);
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
	FREE (tab);
	return err;
}
