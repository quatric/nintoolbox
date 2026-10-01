// Neversoft Guitar Hero (Wii) ".pak.ngc" packages and ".img.ngc" textures;
// see lib-nsgh.h. Both are claimed by double extension plus a structural
// check inside the scanners.
static bool nsgh_has_suffix (ccp path, ccp suffix)
{
	const size_t l = strlen (path), s = strlen (suffix);
	return l >= s && !strcasecmp (path + l - s, suffix);
}

static enumError extract_nsgh_file (ccp arg, ccp basedir, uint depth)
{
	const bool is_pak = nsgh_has_suffix (arg, ".pak.ngc");
	const bool is_img = nsgh_has_suffix (arg, ".img.ngc");
	if (!is_pak && !is_img)
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	if (is_pak)
		err = ScanNsPak (&entries, &n_entries, raw, raw_size);
	else
	{
		ccp slash = strrchr (arg, '/');
		err = ScanNsImg (&entries, &n_entries, raw, raw_size, slash ? slash + 1 : arg);
	}
	FREE (raw);
	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, is_pak ? "NSPAK" : "NSIMG", entries, n_entries);
}
