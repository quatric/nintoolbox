// "NIBM" streamed audio (*.aud, CSI: Hard Evidence, Wii): the payload is an
// Ogg Vorbis stream behind a serialized-class header; see lib-nibm.h.
static enumError extract_nibm_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".aud"))
		return ERR_NOTHING_TO_DO;

	u8 head[4];
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	const size_t got = fread (head, 1, sizeof (head), f);
	fclose (f);
	if (got < 4 || memcmp (head, "NIBM", 4))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	size_t off = 0;
	if (raw_size > UINT_MAX || !IsNibmOgg (raw, raw_size, &off))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// name: <stem>.ogg
	ccp slash = strrchr (arg, '/');
	char name[PATH_MAX];
	snprintf (name, sizeof (name), "%s", slash ? slash + 1 : arg);
	char *dot = strrchr (name, '.');
	if (dot)
		*dot = 0;
	snprintf (name + strlen (name), sizeof (name) - strlen (name), ".ogg");

	nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
	u8 *ogg = entries ? MALLOC (raw_size - off) : 0;
	if (!ogg)
	{
		FREE (entries);
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	memcpy (ogg, raw + off, raw_size - off);
	FREE (raw);
	entries[0].name = STRDUP (name);
	entries[0].data = ogg;
	entries[0].size = (uint)(raw_size - off);
	return write_owned_entries (arg, basedir, depth, "NIBM", entries, 1);
}
