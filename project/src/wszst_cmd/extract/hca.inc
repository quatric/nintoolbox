// CRI HCA audio (".hca", also the members of ADX2 ".awb"/".cpk" archives): decoded with the vendored clHCA decoder (src/clhca, ISC licence) to 16-bit PCM and written
// as "<name>.wav". Encrypted streams (a "ciph" chunk with a non-zero type) are not decoded because the key is game specific. Looping and encoder delay are not applied.
static enumError extract_hca_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".hca"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	clHCA *h = 0;
	s16 *pcm = 0;
	int hs = sz >= 8 ? clHCA_isOurFile (raw, (uint)sz) : -1;
	if (hs <= 0 || (size_t)hs > sz)
		goto done;
	h = clHCA_new ();
	clHCA_stInfo in;
	if (!h || clHCA_DecodeHeader (h, raw, (uint)hs) < 0 || clHCA_getInfo (h, &in) < 0)
		goto done;
	if (!in.blockSize || !in.blockCount || !in.channelCount || in.channelCount > 16 || in.samplingRate < 1000
		|| (u64)hs + (u64)in.blockSize * in.blockCount > sz)
		goto done;
	{
		const size_t spb = 1024, total = spb * in.blockCount;
		pcm = MALLOC (total * in.channelCount * 2);
		s16 *blk = MALLOC (spb * in.channelCount * 2);
		u8 *tmp = MALLOC (in.blockSize);
		bool ok = pcm && blk && tmp;
		for (uint b = 0; ok && b < in.blockCount; b++)
		{
			memcpy (tmp, raw + hs + (size_t)b * in.blockSize, in.blockSize);
			if (clHCA_DecodeBlock (h, tmp, in.blockSize) < 0)
				memset (blk, 0, spb * in.channelCount * 2);
			else
				clHCA_ReadSamples16 (h, blk);
			// clHCA returns channel-interleaved samples per sample index
			memcpy (pcm + b * spb * in.channelCount, blk, spb * in.channelCount * 2);
		}
		FREE (blk);
		FREE (tmp);
		if (!ok)
			goto done;
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".wav");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE HCA:%s (%u ch, %u Hz, %u blocks) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, in.channelCount, in.samplingRate, in.blockCount);
		err = ERR_OK;
		if (!testmode)
		{
			size_t wn = 0;
			u8 *w = nds_strm_make_wav (pcm, (uint)total, in.channelCount, in.samplingRate, &wn);
			err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
			FREE (w);
		}
	}
done:
	if (h)
		clHCA_delete (h);
	FREE (pcm);
	FREE (raw);
	return err;
}
