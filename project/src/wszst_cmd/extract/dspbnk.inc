// Blazing Angels: Squadrons of WWII (Wii) "Data/Sound/_*.bnk" weapon sounds: a single GameCube DSP-ADPCM header (u32 samples, u32 nibble count, u32 rate, loop / format / coefficient
// block as in ".dsp"), then two equal ADPCM halves of ceil(samples / 14) * 8 bytes each, rounded up to 0x20 (left, right); file size = 0x60 + 2 * half. Only one coefficient set is stored, so
// both channels are decoded with it (the right channel decodes to a stable, similarly smooth waveform, which no other coefficient set would). Written as "<name>.wav" (stereo).
// The big multi-sound banks (ambient.bnk, chatter_*.bnk) do not fit the size rule and are left alone.
static enumError extract_dspbnk_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".bnk"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	s16 *pcm = 0;
	if (sz < 0x100)
		goto done;
	{
		const u32 ns = rd_be32 (raw), rate = rd_be32 (raw + 8);
		const size_t need = ((size_t)ns + 13) / 14 * 8, half = (need + 0x1f) / 0x20 * 0x20;
		if (!ns || ns > 0x4000000 || rate < 4000 || rate > 96000 || rd_be32 (raw + 0x18) != 2 || rd_be16 (raw + 0x0e) || 0x60 + 2 * half != sz)
			goto done;
		pcm = MALLOC ((size_t)ns * 2 * 2);
		if (!pcm)
			goto done;
		s16 coef[16];
		for (uint i = 0; i < 16; i++)
			coef[i] = (s16)rd_be16 (raw + 0x1c + 2 * i);
		for (uint ch = 0; ch < 2; ch++)
		{
			const u8 *src = raw + 0x60 + ch * half;
			int h1 = 0, h2 = 0;
			size_t o = 0;
			for (size_t f = 0; f < need / 8 && o < ns; f++, src += 8)
			{
				const int sc = 1 << (src[0] & 15), ci = (src[0] >> 4) & 7;
				for (uint i = 0; i < 14 && o < ns; i++, o++)
				{
					int nib = (src[1 + i / 2] >> ((i & 1) ? 0 : 4)) & 15;
					if (nib >= 8)
						nib -= 16;
					int s = (nib * sc * 2048 + 1024 + coef[2 * ci] * h1 + coef[2 * ci + 1] * h2) >> 11;
					s = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
					h2 = h1;
					h1 = s;
					pcm[o * 2 + ch] = (s16)s;
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
			fprintf (stdlog, "%s%sDECODE BNK-DSP:%s (2 ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, rate, ns);
		err = ERR_OK;
		if (!testmode)
		{
			size_t wn = 0;
			u8 *w = nds_strm_make_wav (pcm, ns, 2, rate, &wn);
			err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
			FREE (w);
		}
	}
done:
	FREE (pcm);
	FREE (raw);
	return err;
}
