// Extract a Brawl PAC archive ("ARC\0"). Retail entries have no filename,
// only a numeric type/index/group, so they use a descriptive generated name.
// Archives made by CreatePAC may carry its path-safe basename extension in
// reserved header bytes; retaining it makes our create/extract cycle lossless.
static const char *pac_type_name (u16 type)
{
	switch (type)
	{
		case 1:
			return "MiscData";
		case 2:
			return "ModelData";
		case 3:
			return "TextureData";
		case 4:
			return "AnimationData";
		case 5:
			return "SceneData";
		case 6:
			return "Type6";
		case 7:
			return "GroupedArchive";
		case 8:
			return "EffectData";
		default:
			return "Unknown";
	}
}

static enumError extract_pac_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pac") && !is_ext (arg, ".pcs") && !is_ext (arg, ".arc"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}
	if (raw_size < 4 || memcmp (raw, "ARC\0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	pac_t pac;
	err = ScanPAC (&pac, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PAC:%s (%u entries, name=%s) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, pac.n_entries, pac.name, dest);

	for (uint i = 0; !err && i < pac.n_entries; i++)
	{
		const pac_entry_t *e = pac.entries + i;
		if (testmode)
			continue;

		char path[PATH_MAX];
		bool use_name = e->name[0] && strcmp (e->name, ".") && strcmp (e->name, "..");
		for (uint j = 0; use_name && j < pac.n_entries; j++)
			if (j != i && !strcmp (e->name, pac.entries[j].name))
				use_name = false;
		if (use_name)
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", e->name);
		else
			snprintf (path, sizeof (path), "%s/%s%04u_%02u.%s.bin", dest, basedir ? basedir : "",
				e->index, e->group_index, pac_type_name (e->type));
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->size && fwrite (e->data, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		const enumError close_err = ResetFile (&F, opt_preserve);
		if (close_err > err)
			err = close_err;
	}

	ResetPAC (&pac);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
