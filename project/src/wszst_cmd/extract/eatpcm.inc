// Meet the Robinsons (Wii) "data_rvl/stream/**/*.eat" PCM streams, big-endian: {u16 1, u16 channels, u32 sample rate, u32 byte rate, u16 block align, u16 bits (16), 0x14 zero bytes, u32 0x48}, then
// signed 16-bit big-endian PCM from 0x24 to the end (interleaved when stereo). Written as "<file>.wav".
static enumError extract_eatpcm_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".eat"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	s16 *pcm = 0;
	const uint ch = sz > 0x40 ? rd_be16 (raw + 2) : 0, rate = sz > 0x40 ? rd_be32 (raw + 4) : 0;
	if (sz < 0x40 || rd_be16 (raw) != 1 || (ch != 1 && ch != 2) || rate < 4000 || rate > 96000 || rd_be32 (raw + 8) != rate * ch * 2 || rd_be16 (raw + 0x0c) != ch * 2 || rd_be16 (raw + 0x0e) != 16)
		goto done;
	{
		const size_t frames = (sz - 0x24) / (2 * ch);
		if (!frames)
			goto done;
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".wav");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE EAT:%s (%u ch, %u Hz, %u frames) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, (uint)frames);
		err = ERR_OK;
		if (!testmode)
		{
			pcm = MALLOC (frames * ch * sizeof (s16));
			if (!pcm)
			{
				err = ERR_OUT_OF_MEMORY;
				goto done;
			}
			for (size_t i = 0; i < frames * ch; i++)
				pcm[i] = (s16)rd_be16 (raw + 0x24 + 2 * i);
			size_t wn = 0;
			u8 *w = nds_strm_make_wav (pcm, (uint)frames, ch, rate, &wn);
			err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
			FREE (w);
		}
	}
done:
	FREE (pcm);
	FREE (raw);
	return err;
}
