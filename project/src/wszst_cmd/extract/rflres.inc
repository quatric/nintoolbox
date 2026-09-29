// Mii resource archives (RFL_Res.dat & co) store several of their 18
// categories as raw BTI textures (see mii_arc_names in lib-rflres.c: eye,
// eyebrow, face_tex, glass_tex, mole, mouth, mustache, nline_tex, cap_tex).
// The rest are proprietary GX-polygon model blobs with no known decoder, so
// extraction dumps every entry as a raw ".bin" for round-tripping regardless
// of category -- but a lone ".bin" is unusable for actually looking at a
// texture. Probe each entry with the existing BTI decoder and, only for the
// ones that are genuinely valid BTI images, additionally save a viewable
// ".png" companion beside the raw ".bin" (same append-style precedent as the
// KCL ".kcl.glb" export: the round-trip source stays untouched).
static void export_rflres_tex_png_if_possible (ccp bin_path)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (bin_path, 0, 0, &raw, &raw_size, 0, 0, 0, false)
		|| raw_size < sizeof (bti_header_t))
	{
		FREE (raw);
		return;
	}

	if (IsValidBTI (raw, (uint)raw_size, 0, 0) >= VALID_ERROR)
	{
		FREE (raw);
		return;
	}

	Image_t img;
	InitializeIMG (&img);
	const enumError err = AssignIMG (&img, -1, raw, (uint)raw_size, 0, false, &be_func, bin_path);
	if (!err)
	{
		char png_path[PATH_MAX];
		const size_t len = strlen (bin_path);
		if (len > 4 && !strcasecmp (bin_path + len - 4, ".bin"))
			snprintf (png_path, sizeof (png_path), "%.*s.png", (int)(len - 4), bin_path);
		else
			snprintf (png_path, sizeof (png_path), "%s.png", bin_path);
		SaveIMG (&img, FF_PNG, 0, 0, png_path, true);
	}
	ResetIMG (&img);
	FREE (raw);
}

static enumError extract_rflres_file (ccp arg, ccp basedir, uint depth)
{
	// Mii resource archives have no magic; their small count-and-offset
	// header can occur by chance in unrelated binary data.  In particular,
	// Pilotwings Resort's Monster Games .mod/.shd resources satisfy the
	// structural probe often enough to be unpacked as bogus Mii archives.
	// Restrict the automatic probe to the filename family used by the Mii
	// Face Library.  This mirrors the repack dispatch below and keeps an
	// extension-less/foreign model from gaining a destructive-looking .d
	// sibling merely because its first few words resemble an offset table.
	ccp ext = strrchr (arg, '.');
	if (!ext
		|| (strcasecmp (ext, ".dat") && strcasecmp (ext, ".rflres") && strcasecmp (ext, ".rfl")
			&& strcasecmp (ext, ".fflres") && strcasecmp (ext, ".ffl")
			&& strcasecmp (ext, ".cflres") && strcasecmp (ext, ".cfl")
			&& strcasecmp (ext, ".aflres") && strcasecmp (ext, ".afl")
			&& strcasecmp (ext, ".nflres") && strcasecmp (ext, ".nfl")
			&& strcasecmp (ext, ".nfl_res")))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err || raw_size < 8 || raw_size > 0x10000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanRFLRes (&entries, &n_entries, raw, (uint)raw_size);
	if (err || !entries || !n_entries)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MIIRES:%s -> %s/ (%u files)\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest, n_entries);

	if (!testmode)
	{
		for (uint i = 0; i < n_entries; i++)
		{
			char path[PATH_MAX];
			snprintf (
				path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", entries[i].name);
			File_t F;
			CreateFileOpt (&F, true, path, false, arg);
			const bool wrote_file = F.f != 0;
			if (F.f && entries[i].size)
				fwrite (entries[i].data, 1, entries[i].size, F.f);
			ResetFile (&F, opt_preserve);
			if (wrote_file)
				export_rflres_tex_png_if_possible (path);
		}
	}

	ResetOwnedEntries (entries, n_entries);
	FREE (raw);

	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
