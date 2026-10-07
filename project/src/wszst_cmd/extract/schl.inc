// Electronic Arts "SCHl" stream (".ngc"/".str" audio and movie files in the
// Wii EA catalogue). The audio is EA ADPCM/XAS, which ffmpeg's EA demuxer
// already understands, so the file is handed to the audio pass-through and
// written as "<name>.wav" in decode mode (wszst xx). Without a decoder the
// file is left untouched.
static enumError extract_schl_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;

	u8 magic[8];
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	const size_t got = fread (magic, 1, sizeof (magic), f);
	fclose (f);
	if (got != sizeof (magic) || memcmp (magic, "SCHl", 4))
		return ERR_NOTHING_TO_DO;

	char wav[PATH_MAX];
	snprintf (wav, sizeof (wav), "%s.wav", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE SCHl:%s -> WAV:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, wav);
	if (testmode)
		return ERR_OK;

	const enumError err = PassthruDecodeAudio (arg, wav);
	if (err == ERR_NOTHING_TO_DO)
		return ERROR0 (ERR_WARNING,
			"%s is an EA SCHl stream, but no mobipeg or ffmpeg was found to decode it"
			" (see --with-mobipeg)",
			arg);
	return err;
}
