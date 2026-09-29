// "Bj" engine sound banks (.bsi, mono PCM8 samples -> name_N.wav) and
// streamed music (.bsm, planar stereo PCM8 -> name.wav).
static enumError bj_write_wav (ccp arg, ccp dest, u8 *wav, size_t wav_size, ccp label)
{
	File_t F;
	enumError err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f && fwrite (wav, 1, wav_size, F.f) != wav_size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", wav_size, dest);
	ResetFile (&F, opt_preserve);
	FREE (wav);
	if (!err && verbose >= 0)
		fprintf (stdlog, "%sEXTRACT %s:%s -> WAV:%s\n", verbose > 0 ? "\n" : "", label, arg, dest);
	return err;
}

static enumError extract_bj_audio_file (ccp arg, ccp basedir, uint depth)
{
	const bool bsi = is_ext (arg, ".bsi");
	if (!bsi && !is_ext (arg, ".bsm"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError result = ERR_OK;
	if (bsi)
	{
		const uint count = BjBsiCount (raw, raw_size);
		if (!count)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		for (uint i = 0; i < count && !result; i++)
		{
			char ext[24], dest[PATH_MAX];
			snprintf (ext, sizeof (ext), "_%u.wav", i);
			beside_source_dest_ext (dest, sizeof (dest), arg, ext);
			if (testmode)
			{
				fprintf (stdlog, "WOULD EXTRACT BJ-SOUND:%s -> WAV:%s\n", arg, dest);
				continue;
			}
			if (blitz_output_current (arg, dest))
				continue;
			u8 *wav = 0;
			size_t wav_size = 0;
			if (DecodeBjBsiSample (&wav, &wav_size, raw, raw_size, i))
				continue;
			result = bj_write_wav (arg, dest, wav, wav_size, "BJ-SOUND");
		}
	}
	else
	{
		char dest[PATH_MAX];
		beside_source_dest_ext (dest, sizeof (dest), arg, ".wav");
		if (testmode)
			fprintf (stdlog, "WOULD EXTRACT BJ-MUSIC:%s -> WAV:%s\n", arg, dest);
		else if (!blitz_output_current (arg, dest))
		{
			u8 *wav = 0;
			size_t wav_size = 0;
			result = DecodeBjMusic (&wav, &wav_size, raw, raw_size);
			if (!result)
				result = bj_write_wav (arg, dest, wav, wav_size, "BJ-MUSIC");
		}
	}
	FREE (raw);
	return result;
}
