// Asobo Sound_Z (Ratatouille .DRV member, DSP-ADPCM) -> WAV.
static enumError extract_asobo_sound_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".sound_z"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (!IsAsoboSound (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".wav");
	if (testmode || blitz_output_current (arg, dest))
	{
		FREE (raw);
		if (testmode)
			fprintf (stdlog, "WOULD EXTRACT ASOBO-SOUND:%s -> WAV:%s\n", arg, dest);
		return ERR_OK;
	}
	u8 *wav = 0;
	size_t wav_size = 0;
	err = DecodeAsoboSound (&wav, &wav_size, raw, raw_size);
	FREE (raw);
	if (err)
		return err;
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f && fwrite (wav, 1, wav_size, F.f) != wav_size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", wav_size, dest);
	ResetFile (&F, opt_preserve);
	FREE (wav);
	if (!err && verbose >= 0)
		fprintf (
			stdlog, "%sEXTRACT ASOBO-SOUND:%s -> WAV:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
