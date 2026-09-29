// Extract a Monster Games PMsh collision resource to a sibling COLLADA .dae.
// PMsh has no four-byte magic, so this is gated on the ".msh" extension and
// DecodeExciteMSH's exact section-size and triangle-index validation.
static enumError extract_msh_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".msh"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MSH:%s -> GLB:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
	{
		err = DecodeExciteMSH (raw, (uint)raw_size, dest);
		// An unedited .msh must not look newer than its .glb on the next
		// CREATE -- see StampFileMtime()'s doc comment -- or every model in
		// a tree gets needlessly re-encoded and repacked.
		if (!err)
			StampFileMtime (dest, arg);
	}
	FREE (raw);
	return err;
}
