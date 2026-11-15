// Pikmin 1 .mod models -> .glb beside the source (geometry only, see lib-pikmin.h).
static enumError extract_pikmin_mod_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".mod"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (!IsPikminMod (raw, raw_size))
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
	err = DecodePikminMod (raw, raw_size, dest);
	FREE (raw);
	if (err > ERR_WARNING)
		return err;
	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT MODEL:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return ERR_OK;
}
