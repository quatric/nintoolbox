// Artefacts Studio ".map" level databases (Diabolik: The Original Sin, Boot
// Camp Academy, ... on Wii). The "FAAFFAAF" tagged-section stream is shared
// with the small .cfg/.gam/.loc files, so the claim needs the stream header
// and closing-tag check of IsDiabolikRes(); see lib-dbkmap.h.
static enumError extract_dbkmap_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".map"))
		return ERR_NOTHING_TO_DO;

	u8 head[8];
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	const size_t got = fread (head, 1, sizeof (head), f);
	fclose (f);
	if (got < 8 || memcmp (head, "\xfa\xaf\xfa\xaf", 4))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	const enumError serr = ScanDbkMap (&entries, &n_entries, raw, raw_size);
	if ((serr && serr != ERR_NOTHING_TO_DO) || !IsDbkMap (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Textures first: writing them also decodes the TPLs to PNGs.
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	err = ERR_OK;
	if (n_entries)
		err = write_owned_entries (arg, basedir, depth, "DBKMAP", entries, n_entries);

	uint models = 0;
	if (!err && !testmode)
	{
		char mdir[PATH_MAX];
		snprintf (mdir, sizeof (mdir), "%s/%s", dest, basedir ? basedir : "");
		models = ExportDbkMapModels (raw, raw_size, mdir);
		if (models && verbose >= 0)
			fprintf (stdlog, "%sEXTRACT DBKMAP:%s (%u models) -> %s/models/\n",
				verbose > 0 ? "\n" : "", arg, models, mdir);
	}
	FREE (raw);
	if (!n_entries && !models)
		return ERR_NOTHING_TO_DO;
	return err;
}
