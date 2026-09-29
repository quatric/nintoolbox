static enumError extract_nud_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".nud"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 16 || (!IsNUD (raw, raw_size)))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT NUD:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}

	model_t *model = ParseNUD (raw, raw_size);
	FREE (raw);
	if (!model)
		return ERR_INVALID_DATA;

	ExportModelToGLB (model, dest);
	FreeModel (model);

	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT NUD:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return ERR_OK;
}
