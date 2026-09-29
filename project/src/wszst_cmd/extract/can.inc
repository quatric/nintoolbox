// A .can is a skeletal animation, not geometry, so unlike .mod it carries no
// texture references and needs no deferred pass -- it converts on sight.
static enumError extract_can_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".can"))
		return ERR_NOTHING_TO_DO;

	// A car model ships e.g. low1t.can next to low1t.mod. extract_mod_file()
	// picks up this same sibling in its own deferred pass and embeds it in
	// low1t.glb alongside the mesh, so producing our own GLB here would just
	// be a redundant, animation-only duplicate of the same data. Skip and let
	// that later pass do the one real write.
	char sibling_mod[PATH_MAX];
	beside_source_dest_ext (sibling_mod, sizeof (sibling_mod), arg, ".mod");
	if (!access (sibling_mod, F_OK))
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

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");

	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT CAN:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}

	err = DecodeExciteCAN (raw, (uint)raw_size, dest);
	FREE (raw);
	if (err)
		return err;

	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT CAN:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
