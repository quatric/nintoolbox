// Metroid Prime Hunters (Nintendo DS): SNDFILE archives (.arc) and *_Model.bin
// models, see lib-mph.h. Models become a .glb beside the source with their
// textures as PNGs; the texture file is the sibling *_Tex.bin when present.
static enumError extract_mph_arc_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (!IsMphArchive (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanMphArchive (&entries, &n_entries, raw, raw_size);
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "MPH-ARC", entries, n_entries);
}

static enumError extract_mph_model_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (!IsMphModel (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT MPH-MODEL:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}

	uint tex_size = 0;
	u8 *tex = LoadMphTexFile (arg, &tex_size);
	err = DecodeMphModel (raw, raw_size, tex, tex_size, dest);
	FREE (raw);
	FREE (tex);
	if (err > ERR_WARNING)
		return err;
	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT MPH-MODEL:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return ERR_OK;
}
