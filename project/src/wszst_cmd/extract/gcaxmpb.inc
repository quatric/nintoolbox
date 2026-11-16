// Marvelous "gcax" sound banks (Harvest Moon: Magical Melody, Wii: sound/*.mpb and *.mlt): big-endian chunks {tag[8], u64 payload size}. "gcaxMLT " wraps "gcaxMPB " (+ "gcaxMSB " sound table,
// not decoded); "gcaxMPB " holds "gcaxMPBW" (the DSP-ADPCM wave data, mono) followed by "gcaxMPBP" (parameters): a 0x20 byte header, then 0x50-byte wave records {u32 samples, u32 nibbles, u32 rate,
// u16 loop flag, u16 format, u32 loop start / end / current, s16 coef[16] at +0x1c, gain, initial ps, hist1 (+0x40), hist2, loop ps / hist, u32 wave offset at +0x4c into the MPBW payload}. Records are read
// while they stay consistent (the trailing instrument records have another layout and are skipped). Each wave is written as "<file>.d/wave_<n>.wav".
static enumError extract_gcaxmpb_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".mpb") && !is_ext (arg, ".mlt"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	s16 *pcm = 0;
	if (sz < 0x60 || memcmp (raw, "gcax", 4))
		goto done;
	{
		size_t w = 0, p = 0;
		for (size_t o = 0; o + 16 <= sz && (!w || !p); o += 16)
		{
			if (!w && !memcmp (raw + o, "gcaxMPBW", 8))
				w = o;
			if (!p && !memcmp (raw + o, "gcaxMPBP", 8))
				p = o;
		}
		if (!w || !p || rd_be32 (raw + w + 8) || rd_be32 (raw + p + 8))
			goto done;
		const u64 wsz = rd_be32 (raw + w + 12), psz = rd_be32 (raw + p + 12);
		if (w + 16 + wsz > sz || p + 16 + psz > sz)
			goto done;
		const u8 *wd = raw + w + 16;
		size_t cnt = 0, r;
		for (r = p + 16 + 0x20; r + 0x50 <= p + 16 + psz; r += 0x50)
		{
			const u32 s = rd_be32 (raw + r), nb = rd_be32 (raw + r + 4), rate = rd_be32 (raw + r + 8), off = rd_be32 (raw + r + 0x4c);
			if (!rate || (u64)nb * 14 / 16 > s + 15 || (u64)nb * 14 / 16 + 15 < s || (nb && (rate < 4000 || rate > 100000)) || off + (u64)(nb + 1) / 2 > wsz)
				break;
			cnt++;
		}
		if (!cnt)
			goto done;
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.d", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE GCAX-MPB:%s (%u waves) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, (uint)cnt, dest);
		err = ERR_OK;
		for (size_t i = 0; i < cnt && !err && !testmode; i++)
		{
			const u8 *h = raw + p + 16 + 0x20 + 0x50 * i;
			const u32 s = rd_be32 (h), nb = rd_be32 (h + 4), rate = rd_be32 (h + 8), off = rd_be32 (h + 0x4c);
			if (!nb)
				continue;
			s16 coef[16];
			for (uint k = 0; k < 16; k++)
				coef[k] = (s16)rd_be16 (h + 0x1c + 2 * k);
			int h1 = (s16)rd_be16 (h + 0x40), h2 = (s16)rd_be16 (h + 0x42);
			const size_t fr = (nb + 15) / 16;
			pcm = MALLOC (fr * 14 * sizeof (s16) + 2);
			if (!pcm)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			size_t ns = 0;
			for (size_t f = 0; f < fr; f++)
			{
				if (off + 8 * f + 8 > wsz)
					break;
				const u8 *src = wd + off + 8 * f;
				const int sc = 1 << (src[0] & 15), ci = (src[0] >> 4) & 7;
				for (uint k = 0; k < 14; k++)
				{
					int v = (src[1 + k / 2] >> ((k & 1) ? 0 : 4)) & 15;
					if (v >= 8)
						v -= 16;
					int x = (v * sc * 2048 + 1024 + coef[2 * ci] * h1 + coef[2 * ci + 1] * h2) >> 11;
					x = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
					h2 = h1;
					h1 = x;
					pcm[ns++] = (s16)x;
				}
			}
			char name[64];
			snprintf (name, sizeof (name), "wave_%03u.wav", (uint)i);
			size_t wn = 0;
			u8 *wav = nds_strm_make_wav (pcm, ns < s ? (uint)ns : s, 1, rate, &wn);
			err = wav ? shade_write (arg, dest, basedir, name, wav, (uint)wn) : ERR_OUT_OF_MEMORY;
			FREE (wav);
			FREE (pcm);
			pcm = 0;
		}
	}
done:
	FREE (pcm);
	FREE (raw);
	return err;
}
