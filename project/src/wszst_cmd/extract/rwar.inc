// NW4R wave data (Wii): ".brwar"/".rwar" archives of RWAV waves and single ".brwav"/".rwav" waves. Every wave is decoded
// (DSP-ADPCM, PCM16, PCM8; mono or stereo) and written as "<name>_NNN.wav" beside the source, with the loop points kept in a
// "smpl" chunk. Wave names live in the matching RWSD, which is not read here.
static enumError extract_rwar_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if ((!is_ext (arg, ".brwar") && !is_ext (arg, ".rwar") && !is_ext (arg, ".brwav") && !is_ext (arg, ".rwav"))
		|| (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 0x20 && (!memcmp (raw, "RWAR", 4) || !memcmp (raw, "RWAV", 4)))
	{
		char dest[PATH_MAX], out[PATH_MAX], stem[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		snprintf (out, sizeof (out), "%s/%s", dest, basedir ? basedir : "");
		const char *sl = strrchr (arg, '/');
		snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (stem, '.');
		if (dot) *dot = 0;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT RWAR:%s -> WAV:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, out);
		if (testmode)
			err = ERR_OK;
		else
		{
			uint n = 0;
			err = ConvertRWARToWAV (raw, sz, out, stem, &n);
		}
	}
	FREE (raw);
	return err;
}
