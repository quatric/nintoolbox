// Microsoft IMA ADPCM WAV files (RIFF/WAVE, format tag 0x11): used inside Miles Sound System banks and by some games directly. Decoded to 16-bit PCM "<file>.pcm.wav" (mono or stereo). Per block and channel a 4-byte header
// {s16 predictor, u8 step index, u8 0} (the predictor is the first sample), followed by 4-byte groups of 8 nibbles per channel, channels interleaved; the sample count comes from the "fact" chunk when present.
static enumError extract_msimawav_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".wav") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	s16 *pcm = 0;
	if (sz < 0x30 || memcmp (raw, "RIFF", 4) || memcmp (raw + 8, "WAVE", 4))
		goto done;
	{
		uint nch = 0, rate = 0, ba = 0, tag = 0;
		size_t dpos = 0, dlen = 0;
		u32 fact = 0;
		for (size_t o = 12; o + 8 <= sz;)
		{
			const u32 cl = rd_le32 (raw + o + 4);
			const u8 *c = raw + o + 8;
			if (!memcmp (raw + o, "fmt ", 4) && cl >= 16 && o + 8 + cl <= sz)
			{
				tag = rd_le16 (c);
				nch = rd_le16 (c + 2);
				rate = rd_le32 (c + 4);
				ba = rd_le16 (c + 12);
			}
			else if (!memcmp (raw + o, "fact", 4) && cl >= 4 && o + 12 <= sz)
				fact = rd_le32 (c);
			else if (!memcmp (raw + o, "data", 4))
			{
				dpos = o + 8;
				dlen = o + 8 + cl > sz ? sz - o - 8 : cl;
				break;
			}
			o += 8 + (size_t)cl + (cl & 1);
		}
		if (tag != 0x11 || nch < 1 || nch > 2 || rate < 1000 || rate > 192000 || ba < 4 * nch + 4 || ba > 0x8000 || !dlen)
			goto done;
		static const s8 itab[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
		static const u16 stab[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
		const uint spb = (ba - 4 * nch) * 2 / nch + 1;
		const size_t blocks = dlen / ba;
		if (!blocks || blocks > 4000000)
			goto done;
		pcm = (s16 *)CALLOC (blocks * spb * nch + 1, sizeof (s16));
		if (!pcm)
			goto done;
		for (size_t b = 0; b < blocks; b++)
		{
			const u8 *bp = raw + dpos + b * ba;
			for (uint c = 0; c < nch; c++)
			{
				int pred = (s16)rd_le16 (bp + c * 4), idx = bp[c * 4 + 2];
				if (idx > 88)
					idx = 88;
				s16 *out = pcm + b * spb * nch + c;
				out[0] = (s16)pred;
				uint k = 1;
				for (uint g = 0; g < (ba - 4 * nch) / (4 * nch); g++)
				{
					const u8 *gp = bp + 4 * nch + (g * nch + c) * 4;
					for (uint j = 0; j < 8; j++)
					{
						const uint nib = (gp[j >> 1] >> ((j & 1) * 4)) & 15;
						const int step = stab[idx];
						int diff = step >> 3;
						if (nib & 1) diff += step >> 2;
						if (nib & 2) diff += step >> 1;
						if (nib & 4) diff += step;
						pred += (nib & 8) ? -diff : diff;
						pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
						idx += itab[nib];
						idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
						out[(size_t)k * nch] = (s16)pred;
						k++;
					}
				}
			}
		}
		uint ns = (uint)(blocks * spb);
		if (fact && fact < ns)
			ns = fact;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%.*s.pcm.wav", (int)(strlen (arg) - 4), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE IMA-ADPCM:%s (%u samples, %u Hz, %u ch) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ns, rate, nch, path);
		err = ERR_OK;
		if (!testmode)
			err = spx_write_wav (arg, path, pcm, ns, nch, rate);
	}
done:
	FREE (pcm);
	FREE (raw);
	return err;
}
