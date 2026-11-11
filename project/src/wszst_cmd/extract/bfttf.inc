// Decode a Nintendo Binary Font TrueType/OpenType Font (.bfttf / .bfotf, same scrambling) to a ".ttf" or ".otf"
// beside the source (or in --dest).
static enumError extract_bfttf_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bfttf") && !is_ext (arg, ".bfotf"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size > UINT_MAX || !IsBFTTF (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *ttf = 0;
	uint ttf_size = 0;
	enumError err = DecodeBFTTF (&ttf, &ttf_size, raw, (uint)raw_size);
	FREE (raw);
	if (err)
		return err;

	// "OTTO" (CFF outlines) is an OpenType font, anything else TrueType.
	const bool otf = ttf_size >= 4 && !memcmp (ttf, "OTTO", 4);
	char dir[PATH_MAX], dest[PATH_MAX];
	beside_source_dir (dir, sizeof (dir), arg);
	ccp sl = strrchr (arg, '/');
	char name[PATH_MAX];
	snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
	char *dot = strrchr (name, '.');
	if (dot)
		*dot = 0;
	snprintf (dest, sizeof (dest), "%s/%s%s", dir, name, otf ? ".otf" : ".ttf");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BFTTF:%s -> %s:%s (%u bytes)\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, otf ? "OTF" : "TTF", dest, ttf_size);

	if (!testmode)
	{
		CreatePath (dir, true);
		err = SaveFile (dest, 0, 0, ttf, ttf_size, 0);
	}

	FREE (ttf);
	return err;
}
