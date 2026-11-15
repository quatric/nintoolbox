// Granny 2 ".gr2" models (big-endian Wii files, see lib-granny.h) -> ".glb" beside the source.
static enumError extract_granny_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".gr2"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (!IsGrannyBE (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT MODEL:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}
	enumError err = DecodeGrannyBE (raw, raw_size, dest);
	if (err == ERR_INVALID_DATA)
	{
		// no meshes (animation / skeleton only): dump the object tree as JSON instead
		beside_source_dest_ext (dest, sizeof (dest), arg, ".json");
		err = DumpGrannyBEJson (raw, raw_size, dest);
		FREE (raw);
		if (err > ERR_WARNING)
			return err == ERR_INVALID_DATA ? ERR_NOTHING_TO_DO : err;
		if (verbose >= 0)
			fprintf (stdlog, "%sEXTRACT GRANNY:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		return ERR_OK;
	}
	FREE (raw);
	if (err > ERR_WARNING)
		return err;
	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT MODEL:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return ERR_OK;
}
