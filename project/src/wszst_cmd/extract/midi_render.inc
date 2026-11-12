#include <dirent.h>
// Standard MIDI files made by the sequence converters (BRSAR/SDAT/RBNK...): with --decode the MIDI is rendered to
// "<name>.wav" through its SoundFont. The SoundFont is found from the "sequences.tsv" manifest next to the MIDI (column
// "soundfont", resolved in ../soundfonts/), or a "<name>.sf2" beside it. A SoundFont only has to exist once per bank.
// Sequences marked "native" in the manifest are rendered by the NintendoWare engine model instead (see below).

// A "native" sequence was rendered by the NintendoWare engine model when its sound archive was extracted. Keeps that
// audio if it is still there (unless --overwrite); otherwise renders the sequence again from the source archive: the
// file "<x>" next to the "<x>.d/midi/" folder. *fallback is set when the archive cannot be found, so the caller can use
// the SoundFont instead.
static enumError midi_render_native (ccp dir, ccp base, bool *fallback)
{
	*fallback = false;
	char stem[PATH_MAX], rdir[PATH_MAX], audio[PATH_MAX], src[PATH_MAX];
	snprintf (stem, sizeof (stem), "%s", base);
	char *dot = strrchr (stem, '.');
	if (dot)
		*dot = 0;
	struct stat st;
	static const char *const ext[] = { ".flac", ".wav" };
	// (right after an archive export the audio is fresh, even with --overwrite)
	for (uint i = 0; i < 2 && (!opt_overwrite || midi_render_quiet); i++)
	{
		snprintf (audio, sizeof (audio), "%s/%s%s", dir, stem, ext[i]);
		if (!stat (audio, &st))
		{
			if (midi_render_quiet)
				return ERR_NOTHING_TO_DO;
			if (verbose >= 0)
				fprintf (stdlog, "ALREADY RENDERED: %s (--overwrite renders it again)\n", audio);
			return ERR_OK;
		}
	}
	// <x>.d/midi -> <x>.d -> <x>
	const size_t dl = realpath (dir, rdir) ? strlen (rdir) : 0;
	if (dl < 8 || strcmp (rdir + dl - 5, "/midi") || memcmp (rdir + dl - 7, ".d", 2))
	{
		*fallback = true;
		return ERR_NOTHING_TO_DO;
	}
	rdir[dl - 5] = 0;
	snprintf (src, sizeof (src), "%.*s", (int)(dl - 7), rdir);
	u8 *raw = 0;
	size_t raw_size = 0;
	if (stat (src, &st) || !S_ISREG (st.st_mode) || LoadFileAlloc (src, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		*fallback = true;
		return ERR_NOTHING_TO_DO;
	}
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sRENDER NATIVE:%s [%s] -> %s/midi/%s.%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			src, stem, rdir, stem, opt_midi_format && *opt_midi_format ? opt_midi_format : "flac");
	enumError err = ERR_OK;
	if (!testmode)
	{
		RSARSourcePath = src;
		err = RenderSoundArchiveSequence (raw, raw_size, rdir, stem);
		RSARSourcePath = 0;
		if (err == ERR_NOTHING_TO_DO)
			*fallback = true;
	}
	FREE (raw);
	return err;
}

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
			char *audio = strchr (sf, '\t');
			if (audio)
				*audio++ = 0;
			if (!strcmp (line, base))
			{
				if (*sf)
					snprintf (sf2, sizeof (sf2), "%s/../soundfonts/%s", dir, sf);
				if (audio && !strcmp (audio, "native"))
				{
					// the accurate rendering; the SoundFont is only a fallback
					fclose (fp);
					fp = 0;
					bool fallback;
					const enumError nerr = midi_render_native (dir, base, &fallback);
					if (!fallback)
						return nerr;
					if (verbose >= 0)
						fprintf (stdlog, "NOTE: source sound archive of %s not found, rendering through the SoundFont\n", arg);
				}
				break;
			}
		}
		if (fp)
			fclose (fp);
	}
	struct stat st;
	if (!*sf2 || stat (sf2, &st))
	{
		snprintf (sf2, sizeof (sf2), "%s", arg);
		char *dot = strrchr (sf2, '.');
		if (dot) strcpy (dot, ".sf2");
		if (stat (sf2, &st))
		{
			// otherwise the only SoundFont in the same folder
			*sf2 = 0;
			DIR *dp = opendir (dir);
			uint found = 0;
			for (struct dirent *de; dp && (de = readdir (dp));)
				if (is_ext (de->d_name, ".sf2") && ++found == 1)
					snprintf (sf2, sizeof (sf2), "%s/%s", dir, de->d_name);
			if (dp)
				closedir (dp);
			if (found != 1)
				return ERR_NOTHING_TO_DO;
		}
	}
	snprintf (wav, sizeof (wav), "%s", arg);
	char *dot = strrchr (wav, '.');
	if (dot) strcpy (dot, ".wav");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sRENDER MIDI:%s + %s -> WAV:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, sf2, wav);
	const enumError err = PassthruRenderMIDI (arg, sf2, wav);
	if (!err && !testmode)
		PassthruWavToFlac (wav);
	if (err == ERR_NOTHING_TO_DO)
		return ERROR0 (ERR_WARNING, "no fluidsynth found to render %s", arg);
	return err;
}
