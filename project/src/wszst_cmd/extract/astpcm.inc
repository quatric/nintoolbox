// Squeeballs Party (Wii) "Music/*.ast" streams, big-endian: {u32 frames, u16 channels, u16 rate, u16 2, u16 0, zero padding up to 0x68}, then frames * channels interleaved
// PCM16 samples (file size = 0x68 + frames * channels * 2). Not the Nintendo "STRM" AST. Written as "<name>.wav".
static enumError extract_astpcm_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".ast"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	s16 *pcm = 0;
	if (sz < 0x70 || !memcmp (raw, "STRM", 4) || !memcmp (raw, "MRTS", 4))
		goto done;
	{
		const u32 frames = rd_be32 (raw);
		const uint ch = rd_be16 (raw + 4), rate = rd_be16 (raw + 6);
		if (!frames || ch < 1 || ch > 2 || rate < 8000 || 0x68 + (u64)frames * ch * 2 != sz)
			goto done;
		for (uint i = 12; i < 0x68; i++)
			if (raw[i])
				goto done;
		pcm = MALLOC ((size_t)frames * ch * 2);
		if (!pcm)
			goto done;
		for (size_t i = 0; i < (size_t)frames * ch; i++)
			pcm[i] = (s16)rd_be16 (raw + 0x68 + 2 * i);
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".wav");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE AST-PCM:%s (%u ch, %u Hz, %u frames) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, frames);
		err = ERR_OK;
		if (!testmode)
		{
			size_t wn = 0;
			u8 *w = nds_strm_make_wav (pcm, frames, ch, rate, &wn);
			err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
			FREE (w);
		}
	}
done:
	FREE (pcm);
	FREE (raw);
	return err;
}
