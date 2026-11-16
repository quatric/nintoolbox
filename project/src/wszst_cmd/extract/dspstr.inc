// Despicable Me: The Game (Wii) "Sounds/<hash>.str" stereo DSP-ADPCM streams, big-endian: {u32 data bytes (file size - 0x54), per channel 0x28 bytes {s16 coef[16], u16 gain, u16 initial
// predictor/scale, s16 hist1, s16 hist2} (left, then right)}, then the ADPCM frames interleaved 8 bytes left / 8 bytes right. No sample rate is stored; 32 kHz is assumed (the same value
// as the other engine variants, not verified against the game). Written as "<name>.wav" (stereo).
static enumError extract_dspstr_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".str"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	s16 *pcm = 0;
	if (sz < 0x54 + 32)
		goto done;
	{
		const size_t db = rd_be32 (raw);
		if (db + 0x54 != sz || db % 16)
			goto done;
		const size_t fr = db / 16;
		for (size_t f = 0; f < fr; f++)
			if ((raw[0x54 + 16 * f] & 0x80) || (raw[0x54 + 16 * f + 8] & 0x80))
				goto done;
		const size_t ns = fr * 14;
		pcm = MALLOC (ns * 4);
		if (!pcm)
			goto done;
		for (uint ch = 0; ch < 2; ch++)
		{
			const u8 *h = raw + 4 + 0x28 * ch;
			s16 coef[16];
			for (uint i = 0; i < 16; i++)
				coef[i] = (s16)rd_be16 (h + 2 * i);
			int h1 = (s16)rd_be16 (h + 0x24), h2 = (s16)rd_be16 (h + 0x26);
			for (size_t f = 0; f < fr; f++)
			{
				const u8 *src = raw + 0x54 + 16 * f + 8 * ch;
				const int sc = 1 << (src[0] & 15), ci = (src[0] >> 4) & 7;
				for (uint i = 0; i < 14; i++)
				{
					int nib = (src[1 + i / 2] >> ((i & 1) ? 0 : 4)) & 15;
					if (nib >= 8)
						nib -= 16;
					int s = (nib * sc * 2048 + 1024 + coef[2 * ci] * h1 + coef[2 * ci + 1] * h2) >> 11;
					s = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
					h2 = h1;
					h1 = s;
					pcm[(f * 14 + i) * 2 + ch] = (s16)s;
				}
			}
		}
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".wav");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE STR-DSP:%s (2 ch, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, (uint)ns);
		err = ERR_OK;
		if (!testmode)
		{
			size_t wn = 0;
			u8 *w = nds_strm_make_wav (pcm, (uint)ns, 2, 32000, &wn);
			err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
			FREE (w);
		}
	}
done:
	FREE (pcm);
	FREE (raw);
	return err;
}
