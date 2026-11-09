// Octomania-style wavetable pair ".wt" + ".pcm" (Kidz Sports / London Taxi / Battle Rage / ... : "sound/roland/gm16pcm.wt", the
// console form of the Roland GM16.DLS bank). The .wt is big endian: six u32 section offsets {0x18, ..., end of file}; the
// last section holds one 16 byte record per sample {u32 rate (low 16 bits, high bits flags), u32 start (in samples), u32
// length (in samples), u32 0} into the companion ".pcm" (big endian s16 mono, consecutive samples). Every sample is written as
// "wave_NNNN.wav" in the "<file>.d" directory. The "...adpcm" variants use compressed samples and are left alone.
static enumError extract_octowt_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".wt"))
		return ERR_NOTHING_TO_DO;
	u8 *wt = 0, *pcm = 0;
	size_t wsz = 0, psz = 0;
	char pp[PATH_MAX];
	snprintf (pp, sizeof (pp), "%s", arg);
	const size_t pl = strlen (pp);
	if (pl < 4 || LoadFileAlloc (arg, 0, 0, &wt, &wsz, 0, 0, 0, false) || !wt || wsz < 0x18 || wsz > 0x4000000)
		goto bad;
	memcpy (pp + pl - 3, ".pcm", 5);
	if (LoadFileAlloc (pp, 0, 0, &pcm, &psz, 0, 0, 0, false) || !pcm || psz < 2)
		goto bad;
	{
		u32 o[6];
		for (uint i = 0; i < 6; i++)
			o[i] = rd_be32 (wt + 4 * i);
		if (o[0] != 0x18 || o[5] != wsz || o[4] >= o[5] || (o[5] - o[4]) % 16)
			goto bad;
		const uint n = (o[5] - o[4]) / 16;
		u64 next = 0;
		for (uint i = 0; i < n; i++)
		{
			const u8 *r = wt + o[4] + 16 * i;
			const u32 off = rd_be32 (r + 4), len = rd_be32 (r + 8), rate = rd_be32 (r) & 0xffff;
			if (!rate || off != next || (u64)(off + len) * 2 > psz)
				goto bad;
			next = (u64)off + len;
		}
		enumError err = ERR_OK;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT OCTOMANIA-WT:%s (%u samples) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n,
				dest);
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *r = wt + o[4] + 16 * i;
			const u32 off = rd_be32 (r + 4), len = rd_be32 (r + 8), rate = rd_be32 (r) & 0xffff;
			const u32 bytes = len * 2;
			u8 *w = MALLOC (44 + (size_t)bytes);
			if (!w)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			memcpy (w, "RIFF", 4);
			w[4] = (u8)(36 + bytes), w[5] = (u8)((36 + bytes) >> 8), w[6] = (u8)((36 + bytes) >> 16), w[7] = (u8)((36 + bytes) >> 24);
			memcpy (w + 8, "WAVEfmt ", 8);
			const u32 hdr[] = { 16, 0x00010001, rate, rate * 2, 0x00100002 };
			for (uint k = 0; k < 5; k++)
				w[16 + 4 * k] = (u8)hdr[k], w[17 + 4 * k] = (u8)(hdr[k] >> 8), w[18 + 4 * k] = (u8)(hdr[k] >> 16), w[19 + 4 * k] = (u8)(hdr[k] >> 24);
			memcpy (w + 36, "data", 4);
			w[40] = (u8)bytes, w[41] = (u8)(bytes >> 8), w[42] = (u8)(bytes >> 16), w[43] = (u8)(bytes >> 24);
			for (u32 k = 0; k < len; k++)
				w[44 + 2 * k] = pcm[2 * ((size_t)off + k) + 1], w[45 + 2 * k] = pcm[2 * ((size_t)off + k)];
			char name[32];
			snprintf (name, sizeof (name), "wave_%04u.wav", i);
			err = shade_write (arg, dest, basedir, name, w, 44 + bytes);
			FREE (w);
		}
		FREE (wt);
		FREE (pcm);
		return err;
	}
bad:
	FREE (wt);
	FREE (pcm);
	return ERR_NOTHING_TO_DO;
}
