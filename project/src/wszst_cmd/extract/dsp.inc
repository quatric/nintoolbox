// Nintendo GameCube/Wii DSP-ADPCM standalone audio (.dsp) -> WAV.
static enumError extract_dsp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dsp"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (!IsDSP (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest && *opt_dest)
	{
		if (IsDirectory (opt_dest, false))
		{
			ccp base = FindFilename (arg, 0);
			size_t blen = strlen (base);
			if (blen > 4 && !strcasecmp (base + blen - 4, ".dsp"))
				blen -= 4;
			snprintf (dest, sizeof (dest), "%s/%.*s.wav", opt_dest, (int)blen, base);
		}
		else if (is_ext (opt_dest, ".wav"))
		{
			snprintf (dest, sizeof (dest), "%s", opt_dest);
		}
		else
		{
			snprintf (dest, sizeof (dest), "%s.wav", opt_dest);
		}
	}
	else
	{
		beside_source_dest_ext (dest, sizeof (dest), arg, ".wav");
	}
	if (testmode || blitz_output_current (arg, dest))
	{
		FREE (raw);
		if (testmode)
			fprintf (stdlog, "WOULD EXTRACT DSP:%s -> WAV:%s\n", arg, dest);
		return ERR_OK;
	}

	u8 *wav = 0;
	size_t wav_size = 0;
	err = DecodeDSPToWAV (raw, raw_size, &wav, &wav_size);
	FREE (raw);
	if (err)
		return err;

	err = SaveFile (dest, 0, 0, wav, (uint)wav_size, 0);
	FREE (wav);
	if (!err && verbose >= 0)
		fprintf (stdlog, "%sEXTRACT DSP:%s -> WAV:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
