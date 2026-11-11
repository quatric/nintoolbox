// Standard MIDI files made by the sequence converters (BRSAR/SDAT/RBNK...): with --decode the MIDI is rendered to
// "<name>.wav" through its SoundFont. The SoundFont is found from the "sequences.tsv" manifest next to the MIDI (column
// "soundfont", resolved in ../soundfonts/), or a "<name>.sf2" beside it. A SoundFont only has to exist once per bank.
static enumError extract_midi_render_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir; (void)depth;
	if ((!is_ext (arg, ".mid") && !is_ext (arg, ".midi")) || !opt_decode)
		return ERR_NOTHING_TO_DO;
	char dir[PATH_MAX], base[PATH_MAX], sf2[PATH_MAX] = "", wav[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", arg);
	char *sl = strrchr (dir, '/');
	if (sl) { snprintf (base, sizeof (base), "%s", sl + 1); *sl = 0; }
	else { snprintf (base, sizeof (base), "%s", arg); snprintf (dir, sizeof (dir), "."); }
	// manifest lookup
	char tsv[PATH_MAX];
	snprintf (tsv, sizeof (tsv), "%s/sequences.tsv", dir);
	FILE *fp = fopen (tsv, "r");
	if (fp)
	{
		char line[1024];
		while (fgets (line, sizeof (line), fp))
		{
			char *tab = strchr (line, '\t');
			if (!tab) continue;
			*tab = 0;
			char *sf = tab + 1;
			sf[strcspn (sf, "\r\n")] = 0;
			if (!strcmp (line, base) && *sf)
			{
				snprintf (sf2, sizeof (sf2), "%s/../soundfonts/%s", dir, sf);
				break;
			}
		}
		fclose (fp);
	}
	struct stat st;
	if (!*sf2 || stat (sf2, &st))
	{
		snprintf (sf2, sizeof (sf2), "%s", arg);
		char *dot = strrchr (sf2, '.');
		if (dot) strcpy (dot, ".sf2");
		if (stat (sf2, &st))
			return ERR_NOTHING_TO_DO;
	}
	snprintf (wav, sizeof (wav), "%s", arg);
	char *dot = strrchr (wav, '.');
	if (dot) strcpy (dot, ".wav");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sRENDER MIDI:%s + %s -> WAV:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, sf2, wav);
	const enumError err = PassthruRenderMIDI (arg, sf2, wav);
	if (err == ERR_NOTHING_TO_DO)
		return ERROR0 (ERR_WARNING, "no fluidsynth found to render %s", arg);
	return err;
}
