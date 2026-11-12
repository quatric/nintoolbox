// Wii U boot sound (".btsnd"): u32 BE type (0 or 2), u32 BE loop start, then 48 kHz stereo PCM16 big endian interleaved per sample (after
// vgmstream btsnd.c). Decoded to "<name>.wav".
static enumError extract_btsnd_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".btsnd") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const uint type = sz > 8 ? raw[0] << 24 | raw[1] << 16 | raw[2] << 8 | raw[3] : 9;
	const uint loop = sz > 8 ? raw[4] << 24 | raw[5] << 16 | raw[6] << 8 | raw[7] : 0;
	const size_t frames = sz > 8 ? (sz - 8) / 4 : 0;
	if ((type == 0 || type == 2) && frames && frames <= 960000 && loop < frames)
	{
		s16 *pcm = MALLOC (frames * 4);
		if (pcm)
		{
			for (size_t i = 0; i < frames * 2; i++)
				pcm[i] = (s16)(raw[8 + 2 * i] << 8 | raw[9 + 2 * i]);
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot) strcpy (dot, ".wav");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE WIIU-BTSND:%s (2 ch, 48000 Hz, %zu samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, frames);
			if (testmode)
				err = ERR_OK;
			else
			{
				size_t wn = 0;
				u8 *w = nds_strm_make_wav (pcm, (uint)frames, 2, 48000, &wn);
				err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
				FREE (w);
			}
			FREE (pcm);
		}
	}
	FREE (raw);
	return err;
}
