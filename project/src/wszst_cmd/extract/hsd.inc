//-----------------------------------------------------------------------------
// HAL Laboratory "sysdolphin" (HSD) .dat object archives -- Super Smash Bros.
// Melee, Kirby Air Ride and the "TV no Tomo" Wii channel. These carry no magic
// (identification is structural, see IsHSD()), so the file must be read before
// it can be declined; the extension gate keeps that cheap.
//
// Destination: textures land beside the .dat, which is exactly right for a
// file reached by the recursive tree pre-pass -- that tree already lives below
// the requested destination, and extract_tree_complete() deliberately clears
// opt_dest for the whole walk so every asset keeps its path. A .dat named
// directly on the command line has no such tree, so an explicit --dest must be
// honoured instead; opt_dest is only ever set at that top level. Exported PNG
// names carry the source's basename as a prefix, so a shared --dest directory
// cannot collide even for a whole batch of .dat files.
//
// Return value: ERR_OK once the file has been positively identified, else
// ERR_NOTHING_TO_DO. These archives are not directory-style containers, so
// letting the caller's chain continue past a positive identification only ends
// in a bogus "destination already exists" failure from the generic extractor.

static void hal_texture_dest (char *dest, uint dest_size, ccp arg)
{
	if (opt_dest && *opt_dest)
	{
		snprintf (dest, dest_size, "%s", opt_dest);
		uint n = strlen (dest);
		while (n > 1 && dest[n - 1] == '/')
			dest[--n] = 0;
		return;
	}

	snprintf (dest, dest_size, "%s", arg);
	char *slash = strrchr (dest, '/');
	if (slash)
		*slash = 0;
	else
		snprintf (dest, dest_size, ".");
}

static enumError extract_hsd_textures (ccp arg, ccp basedir, uint depth)
{
	// ".mdl" only for HSD archive bundles (Doraemon GC map*_dat.mdl): many
	// complete archives in one file, checked structurally below.
	if (!is_ext (arg, ".dat") && !is_ext (arg, ".mdl"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	const bool is_bundle
		= raw_size >= 0x40 && raw_size <= 0x40000000 && IsHSDBundle (raw, (uint)raw_size);
	if (raw_size < 0x40 || raw_size > 0x40000000 || (!is_bundle && !IsHSD (raw, (uint)raw_size)))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	hal_texture_dest (dest, sizeof (dest), arg);
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;

	if (is_bundle && !testmode)
	{
		// All archives merge into one <name>.glb; each archive's textures
		// land beside it as <name>_NNN.texMMM.png.
		char glb_path[PATH_MAX];
		snprintf (glb_path, sizeof (glb_path), "%s/%s.glb", dest, base);
		CreatePath (glb_path, false);
		const int nm = ExportHSDBundleModel (raw, (uint)raw_size, glb_path);
		if (nm > 0 && verbose >= 0)
			fprintf (stdlog,
				"  - HSD bundle model (%u archives, %d mesh%s) extracted from %s -> %s\n",
				CountHSDBundle (raw, (uint)raw_size), nm, nm == 1 ? "" : "es", arg, glb_path);
	}
	else if (!testmode)
	{
		const int n = ExportHSDTexturesFromData (raw, (uint)raw_size, dest, base);
		if (n > 0 && verbose >= 0)
			fprintf (stdlog, "  - %u HSD texture%s extracted from %s -> %s/\n", n,
				n == 1 ? "" : "s", arg, dest);

		// Static JOBJ/DOBJ/POBJ geometry (see lib-hsd.h for scope: no
		// ENVELOPE/SHAPEANIM weighting or material/texture binding yet).
		char glb_path[PATH_MAX];
		snprintf (glb_path, sizeof (glb_path), "%s/%s.glb", dest, base);
		const int nm = ExportHSDModelFromData (raw, (uint)raw_size, glb_path);
		if (nm > 0 && verbose >= 0)
			fprintf (stdlog, "  - HSD model (%d mesh%s) extracted from %s -> %s\n", nm,
				nm == 1 ? "" : "es", arg, glb_path);
	}
	else if (verbose >= 0)
		fprintf (stdlog, "  - WOULD extract HSD textures/model from %s -> %s/\n", arg, dest);
	FREE (raw);
	(void)basedir;
	(void)depth;
	return ERR_OK;
}

static enumError extract_hsd_textures_tree (ccp root, uint depth)
{
	if (depth > 32)
		return ERR_FILE_TOO_BIG;
	DIR *dir = opendir (root);
	if (!dir)
		return ERR_NOT_EXISTS;
	struct dirent *de;
	while ((de = readdir (dir)))
	{
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char path[PATH_MAX];
		const int len = snprintf (path, sizeof (path), "%s/%s", root, de->d_name);
		if (len < 0 || (uint)len >= sizeof (path))
			continue;
		struct stat st;
		if (lstat (path, &st))
			continue;
		if (S_ISDIR (st.st_mode))
			extract_hsd_textures_tree (path, depth + 1);
		else if (S_ISREG (st.st_mode))
			extract_hsd_textures (path, 0, depth);
	}
	closedir (dir);
	return ERR_OK;
}
