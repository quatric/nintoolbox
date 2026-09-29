// Torus Games hunkfile (.hnk, Wii): textures -> PNG, DSP-ADPCM streams (raw
// files in the sibling ../SOUND directory) -> WAV, in a directory named after
// the hunkfile.
static enumError extract_torus_hnk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".hnk"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	uint count = 0;
	torus_asset_t *list = IsTorusHnk (raw, raw_size) ? ListTorusHnk (raw, raw_size, &count) : 0;
	if (!list || !count)
	{
		FREE (list);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX], sounddir[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	StringCopyS (sounddir, sizeof (sounddir), arg);
	char *slash = strrchr (sounddir, '/');
	if (slash)
		*slash = 0;
	else
		StringCopyS (sounddir, sizeof (sounddir), ".");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT TORUS-HNK:%s (%u assets) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);

	enumError err = ERR_OK;
	for (uint i = 0; i < count && !err && !testmode; i++)
	{
		const torus_asset_t *a = list + i;
		// unique file name (the file system is case-insensitive)
		char path[PATH_MAX], name[160];
		const ccp ext = a->kind == TORUS_TEXTURE ? "png" : "wav";
		uint dup = 0;
		for (;;)
		{
			bool clash = false;
			snprintf (name, sizeof (name), dup ? "%s_%u" : "%s", a->name, dup);
			for (uint j = 0; j < i && !clash; j++)
				clash = list[j].kind == a->kind && !strcasecmp (list[j].name, name);
			// earlier duplicates were renamed in place
			if (!clash)
				break;
			dup++;
		}
		char safe_name[160];
		if (!OwnedNameOk (name))
			snprintf (safe_name, sizeof (safe_name), "asset_%04u", i);
		else
			snprintf (safe_name, sizeof (safe_name), "%s", name);
		snprintf (path, sizeof (path), "%s/%s%s.%s", dest, basedir ? basedir : "", safe_name, ext);
		if (a->kind == TORUS_TEXTURE)
		{
			u8 *rgba = 0;
			if (DecodeTorusTexture (&rgba, a))
				continue;
			err = SaveDecodedRGBAToPNG (rgba, a->width, a->height, &be_func, path, 0, true);
		}
		else
		{
			if (!OwnedNameOk (a->raw) || strstr (a->raw, ".."))
				continue;
			char rawpath[PATH_MAX];
			snprintf (rawpath, sizeof (rawpath), "%s/../SOUND/%s", sounddir, a->raw);
			u8 *snd = 0, *wav = 0;
			size_t snd_size = 0, wav_size = 0;
			struct stat rst;
			if (stat (rawpath, &rst)
				|| LoadFileAlloc (rawpath, 0, 0, &snd, &snd_size, 0, 0, 0, false) || !snd)
				continue;
			const enumError derr = DecodeTorusStream (&wav, &wav_size, a, snd, snd_size);
			FREE (snd);
			if (derr)
				continue;
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (wav, 1, wav_size, F.f) != wav_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", wav_size, path);
			ResetFile (&F, opt_preserve);
			FREE (wav);
		}
		// remember the unique name for later duplicate checks
		snprintf ((char *)a->name, sizeof (a->name), "%s", name);
	}
	FREE (list);
	FREE (raw);
	return err;
}
