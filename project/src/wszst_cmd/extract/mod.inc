static enumError extract_mod_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".mod"))
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

	// A .mod names its textures but does not carry them: they are sibling
	// .tex files that this same tree pass decodes to PNG. Converting here
	// would run before most of those PNGs exist, so the GLB would come out
	// with named-but-empty image entries -- on Mantis.car only 2 of 18
	// textures resolved. Defer to export_models_tree(), which runs once the
	// whole tree is extracted and indexed, exactly as HSF does above.
	if (g_in_model_deferred_pass)
	{
		FREE (raw);
		return ERR_OK;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");

	// A sibling .can (see extract_can_file(), which defers to this pass
	// rather than write its own animation-only GLB) is the skeletal
	// animation for this same part; embed it in the one GLB this call
	// writes instead of leaving it as a second file.
	char sibling_can[PATH_MAX];
	beside_source_dest_ext (sibling_can, sizeof (sibling_can), arg, ".can");
	u8 *can_raw = 0;

	// Capcom MT Framework Mobile shares ".mod" with Monster Games NDL
	// (SPICA MTModel); the "MOD\0" magic routes to its own decoder so an
	// MT model never reaches DecodeExciteMOD below.
	if (raw_size >= 4 && !memcmp (raw, "MOD", 3) && !raw[3] && IsMTMOD (raw, raw_size))
	{
		char mtdest[PATH_MAX];
		beside_source_dest_ext (mtdest, sizeof (mtdest), arg, ".glb");
		if (testmode)
		{
			FREE (raw);
			fprintf (stdlog, "WOULD EXTRACT MTMOD:%s -> GLB:%s\n", arg, mtdest);
			return ERR_OK;
		}
		char sibdir[PATH_MAX];
		snprintf (sibdir, sizeof (sibdir), "%s", arg);
		char *slash = strrchr (sibdir, '/');
		if (slash)
			*slash = 0;
		else
			snprintf (sibdir, sizeof (sibdir), ".");
		model_t *model = ParseMTMOD (raw, raw_size, sibdir);
		FREE (raw);
		if (!model)
			return ERROR0 (ERR_INVALID_DATA, "Failed to decode MT MOD: %s\n", arg);
		err = ExportModelToGLB (model, mtdest);
		FreeModel (model);
		if (err)
			return err;
		StampFileMtime (mtdest, arg);
		if (verbose >= 0)
			fprintf (
				stdlog, "%sEXTRACT MTMOD:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, mtdest);
		return ERR_OK;
	}
	size_t can_raw_size = 0;
	const bool have_can = !access (sibling_can, F_OK)
		&& !LoadFileAlloc (sibling_can, 0, 0, &can_raw, &can_raw_size, 0, 0, 0, false)
		&& can_raw_size <= UINT_MAX;

	if (testmode)
	{
		FREE (raw);
		FREE (can_raw);
		fprintf (stdlog, "WOULD EXTRACT MOD:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}

	err = DecodeExciteMOD (
		raw, (uint)raw_size, have_can ? can_raw : 0, have_can ? (uint)can_raw_size : 0, dest);
	FREE (raw);
	FREE (can_raw);
	if (err)
		return err;

	// An unedited .mod must not look newer than its .glb on the next CREATE
	// -- see StampFileMtime()'s doc comment -- or every model in a tree gets
	// needlessly re-encoded and repacked (that's what create_archive_dispatch.inc's
	// repack pass compares against; it never inspects the sibling .can).
	StampFileMtime (dest, arg);

	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT MOD:%s -> GLB:%s%s\n", verbose > 0 ? "\n" : "", arg, dest,
			have_can ? " (+CAN)" : "");
	return err;
}
