static enumError extract_mpr_cmdl_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".cmdl") && !is_ext (arg, ".smdl"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 16 || (!IsMPRCMDL (raw, raw_size)))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT MPR-CMDL:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}

	// Texture PNGs resolve through the tree-wide index built after
	// extraction completes: defer exactly like the MOD/HSF extractors so
	// export_models_tree() converts with the index ready. Direct
	// single-file runs (flag off) still convert immediately.
	if (g_in_model_deferred_pass)
	{
		FREE (raw);
		return ERR_OK;
	}

	model_t *model = ParseMPRCMDL (raw, raw_size);
	FREE (raw);
	if (!model)
		return ERR_INVALID_DATA;

	ExportModelToGLB (model, dest);
	FreeModel (model);

	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT MPR-CMDL:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return ERR_OK;
}
