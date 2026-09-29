// Load every CHR0 in one extracted "AnmChr(NW4R)" directory into `model`.
// Extracted BRRES members keep their raw resource name verbatim -- no
// ".chr0" extension is added (unlike, say, texture PNGs) -- so a sidecar
// ".txt" dump is the only thing to actually skip; everything else is a CHR0.
static void import_chr0_dir (model_t *model, const char *anm_dir)
{
	DIR *dp = opendir (anm_dir);
	if (!dp)
		return;
	struct dirent *de;
	while ((de = readdir (dp)))
	{
		if (is_ext (de->d_name, ".txt"))
			continue;
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char chr0_path[PATH_MAX];
		snprintf (chr0_path, sizeof (chr0_path), "%s/%s", anm_dir, de->d_name);
		u8 *cdata = 0;
		size_t csize = 0;
		if (LoadFileAlloc (chr0_path, 0, 0, &cdata, &csize, 0, 0, 0, false) == ERR_OK)
		{
			if (csize >= 4 && !memcmp (cdata, "CHR0", 4))
				ParseCHR0IntoModel (model, cdata, csize, de->d_name);
			FREE (cdata);
		}
	}
	closedir (dp);
}

// A BRRES's MDL0 lives under ".../3DModels(NW4R)/name"; CHR0 siblings
// extract to ".../AnmChr(NW4R)/*" right next to it when model and animation
// share one BRRES. Brawl's own fighter data instead splits a character into
// three sibling BRRES containers extracted as "NNNN_NN.ModelData.bin.d",
// "NNNN_NN.AnimationData.bin.d" and "...TextureData.bin.d" (see e.g.
// DATA/files/fighter/*/Fit*.pac), so also check the AnimationData.bin.d
// directory with the same numeric prefix, in the same parent, when the
// model's own directory is named *ModelData.bin.d.
static void import_chr0_siblings_for_mdl0 (model_t *model, const char *arg)
{
	char argcopy[PATH_MAX];
	snprintf (argcopy, sizeof (argcopy), "%s", arg);
	char *slash = strrchr (argcopy, '/');
	char *mdl_pos = slash ? strstr (argcopy, "3DModels(NW4R)") : NULL;
	if (!mdl_pos)
		return;
	const size_t prefix_len = mdl_pos - argcopy;

	char anm_dir[PATH_MAX];
	snprintf (anm_dir, sizeof (anm_dir), "%.*sAnmChr(NW4R)", (int)prefix_len, argcopy);
	import_chr0_dir (model, anm_dir);

	// prefix_len includes the trailing '/'; the BRRES-extraction directory
	// name itself is the path component just before it.
	if (prefix_len < 2)
		return;
	char brres_dir[PATH_MAX];
	snprintf (brres_dir, sizeof (brres_dir), "%.*s", (int)(prefix_len - 1), argcopy);
	char *dir_slash = strrchr (brres_dir, '/');
	char *dir_name = dir_slash ? dir_slash + 1 : brres_dir;
	char *model_tag = strstr (dir_name, "ModelData.bin.d");
	if (!model_tag || model_tag[strlen ("ModelData.bin.d")] != 0)
		return;

	char anim_brres_dir[PATH_MAX];
	snprintf (anim_brres_dir, sizeof (anim_brres_dir), "%.*sAnimationData.bin.d",
		(int)(model_tag - brres_dir), brres_dir);
	char anim_anm_dir[PATH_MAX];
	snprintf (anim_anm_dir, sizeof (anim_anm_dir), "%s/AnmChr(NW4R)", anim_brres_dir);
	import_chr0_dir (model, anim_anm_dir);
}

// Export a 3D model file (MDL0 from a BRRES, or a standalone NSBMD/BCRES/
// BCH/BFRES container) to DAE, mirroring what `wmdlt ENCODE -d out.dae`
// does standalone. XX/XEXPORT's extraction pipeline used to only decode
// BRRES down to its raw MDL0/texture/anim members -- the model geometry
// itself was written out but never converted, so a real disc's 3D models
// (e.g. Kirby's Epic Yarn's .gfa -> .brres -> 3DModels(NW4R)/*) never
// produced a .dae despite ParseMDL0()/ExportModelToDAE() already existing
// and being wired into wmdlt. Only runs when export_count>0 (XX aliases to
// XEXPORT, which sets it) so plain XDECODE/XALL keep their existing output.
static enumError export_model_if_possible (ccp arg)
{
	if (export_count <= 0)
		return ERR_NOTHING_TO_DO;

	// A recursive post-pass visits a complete extracted game tree. Probe the
	// four-byte signature before loading a file so unrelated sounds, scripts,
	// and archives are not needlessly read into memory just to be declined.
	// Eight bytes are probed so J3D GameCube/Wii models (J3D2bmd3/J3D2bdl4)
	// are told apart from Nintendo DS BMD0 models sharing the .bmd suffix.
	u8 magic[8];
	FILE *probe = fopen (arg, "rb");
	if (!probe)
		return ERR_NOT_EXISTS;
	const size_t n_magic = fread (magic, 1, sizeof (magic), probe);
	fclose (probe);
	const bool is_bmd_file = is_ext (arg, ".bmd");
	const bool is_j3d = n_magic >= 8 && !memcmp (magic, "J3D2", 4)
		&& (!memcmp (magic + 4, "bmd3", 4) || !memcmp (magic + 4, "bmd2", 4)
			|| !memcmp (magic + 4, "bdl4", 4));
	if (n_magic < 4
		|| ((memcmp (magic, "BMD0", 4) && memcmp (magic, "CGFX", 4) && memcmp (magic, "BCH\0", 4)
				&& memcmp (magic, "FRES", 4) && memcmp (magic, "MDL0", 4)
				&& memcmp (magic, "RFRM", 4) && !is_bmd_file)
			&& !is_j3d))
		return ERR_NOTHING_TO_DO;

	u8 *data = 0;
	size_t size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &data, &size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	model_t *model = 0;
	if ((size >= 8 && !memcmp (data, "J3D2", 4)
			&& (!memcmp (data + 4, "bmd3", 4) || !memcmp (data + 4, "bmd2", 4)
				|| !memcmp (data + 4, "bdl4", 4))))
	{
		char dest_probe[PATH_MAX];
		if (opt_dest)
			SubstDest (dest_probe, sizeof (dest_probe), arg, opt_dest, 0, ".glb", false);
		else
			snprintf (dest_probe, sizeof (dest_probe), "%s.glb", arg);
		if (!testmode)
			ExportJ3DTexturesFromData (data, size, dest_probe);
		model = ParseJ3D (data, size);
	}
	else if ((size >= 4 && !memcmp (data, "BMD0", 4)) || is_bmd_file)
	{
		char dest_probe[PATH_MAX];
		if (opt_dest)
			SubstDest (dest_probe, sizeof (dest_probe), arg, opt_dest, 0, ".glb", false);
		else
			snprintf (dest_probe, sizeof (dest_probe), "%s.glb", arg);
		if (!testmode)
		{
			// Real BMD0 files carry their textures in a sibling TEX0
			// block; ExportEarlyDSBMDTextures only understands the legacy
			// pre-BMD0 flat header and is a harmless no-op here.
			ExportNitroTEX0Textures (data, (uint)size, dest_probe);
			ExportEarlyDSBMDTextures (data, size, dest_probe);
		}
		model = ParseNSBMD (data, size);
	}
	else if (size >= 4 && !memcmp (data, "CGFX", 4))
	{
		char dest_probe[PATH_MAX];
		if (opt_dest)
			SubstDest (dest_probe, sizeof (dest_probe), arg, opt_dest, 0, ".glb", false);
		else
			snprintf (dest_probe, sizeof (dest_probe), "%s.glb", arg);
		if (!testmode)
			ExportBCRESTexturesFromData (data, size, dest_probe);
		model = ParseBCRES (data, size);
	}
	else if (size >= 4 && !memcmp (data, "BCH\0", 4))
	{
		char dest_probe[PATH_MAX];
		if (opt_dest)
			SubstDest (dest_probe, sizeof (dest_probe), arg, opt_dest, 0, ".glb", false);
		else
			snprintf (dest_probe, sizeof (dest_probe), "%s.glb", arg);
		if (!testmode)
			ExportBCHTexturesFromData (data, (uint)size, dest_probe);
		model = (model_t *)ParseBCH (data, (uint)size);
	}
	else if (size >= 4 && !memcmp (data, "FRES", 4))
	{
		model = ParseBFRES (data, size);
		if (!model)
			model = ParseBFRESSwitch (data, size);
	}
	else if (size >= 4 && !memcmp (data, "MDL0", 4))
	{
		model = ParseMDL0 (data, size);
		if (model)
			import_chr0_siblings_for_mdl0 (model, arg);
	}
	else if (size >= 0x20 && !memcmp (data, "RFRM", 4) && IsMPRCMDL (data, size))
		model = ParseMPRCMDL (data, size);
	else if (size >= 8 && IsMPRSKEL (data, size))
		model = ParseMPRSKEL (data, size);
	FREE (data);
	if (!model)
		return ERR_NOTHING_TO_DO;
	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".glb", false);
	else
		snprintf (dest, sizeof (dest), "%s.glb", arg);
	if (opt_dest && !is_ext (dest, ".glb"))
	{
		FreeModel (model);
		return ERR_NOTHING_TO_DO;
	}
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXPORT MODEL:%s -> GLB:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);
	if (!testmode)
		err = (ExportModelToGLB (model, dest))
			? ERROR0 (ERR_WRITE_FAILED, "Failed to write GLB: %s\n", dest)
			: ERR_OK;
	FreeModel (model);
	return err;
}

// Convert a BRSAR (Wii sound archive) found in an extracted tree to MIDI+SF2.
// wszst itself does not link vgmtrans (it needs cmake+glib, which the
// Makefile deliberately keeps out of the main tool -- see NO_VGMTRANS), so
// this shells out to the sibling `wbrsar` binary that statically links it,
// the same pass-through style lib-passthru.c already uses for ctrtool/
// ndstool/sharpii/wit. Without this, "wszst XX" on a tree containing a
// .brsar silently left it as a raw, unconverted member. Only runs when
// export_count>0 (XX aliases to XEXPORT) so plain XDECODE/XALL are unaffected.
static enumError convert_brsar_if_possible (ccp arg)
{
	if (export_count <= 0)
		return ERR_NOTHING_TO_DO;

	// Probe the four-byte signature before shelling out, same discipline as
	// export_model_if_possible() -- most files in a real tree are neither.
	u8 magic[4];
	FILE *probe = fopen (arg, "rb");
	if (!probe)
		return ERR_NOT_EXISTS;
	const size_t n_magic = fread (magic, 1, sizeof (magic), probe);
	fclose (probe);

	bool is_rsar = (n_magic == sizeof (magic) && !memcmp (magic, "RSAR", 4));
	bool is_sdat = (n_magic == sizeof (magic) && !memcmp (magic, "SDAT", 4));
	bool is_sound = is_rsar || is_sdat;

	if (!is_sound)
		return ERR_NOTHING_TO_DO;

	// Prefer the wbrsar built alongside this binary (same install/build dir)
	// over whatever a bare PATH search might turn up first.
	// If wbrsar is not available or if --with-vgmtrans is set, fall back to
	// external vgmtrans (magcius/vgmtrans).
	char tool[PATH_MAX] = { 0 };
	bool using_vgmtrans = false;

	if (opt_with_vgmtrans && *opt_with_vgmtrans)
	{
		if (!access (opt_with_vgmtrans, X_OK))
		{
			snprintf (tool, sizeof (tool), "%s", opt_with_vgmtrans);
			using_vgmtrans = true;
		}
	}

	char mypath[PATH_MAX];
	GetProgramPath (mypath, sizeof (mypath), true, 0);
	ccp slash = strrchr (mypath, '/');

	if (!*tool)
	{
		if (slash)
		{
			snprintf (tool, sizeof (tool), "%.*s/wbrsar", (int)(slash - mypath), mypath);
			if (access (tool, X_OK))
				*tool = 0;
		}
	}
	if (!*tool)
	{
		ccp dirs = getenv ("PATH");
		while (dirs && *dirs && !*tool)
		{
			ccp end = strchr (dirs, ':');
			const uint len = end ? (uint)(end - dirs) : (uint)strlen (dirs);
			if (len)
			{
				char cand[PATH_MAX];
				snprintf (cand, sizeof (cand), "%.*s/wbrsar", (int)len, dirs);
				if (!access (cand, X_OK))
					snprintf (tool, sizeof (tool), "%s", cand);
			}
			dirs = end ? end + 1 : 0;
		}
	}
	if (!*tool)
	{
		// Fall back to external vgmtrans binary (magcius/vgmtrans)
		if (slash)
		{
			snprintf (tool, sizeof (tool), "%.*s/vgmtrans", (int)(slash - mypath), mypath);
			if (access (tool, X_OK))
			{
				snprintf (tool, sizeof (tool), "%.*s/vgmtrans-cli", (int)(slash - mypath), mypath);
				if (access (tool, X_OK))
					*tool = 0;
			}
			if (*tool)
				using_vgmtrans = true;
		}
		if (!*tool)
		{
			ccp dirs = getenv ("PATH");
			while (dirs && *dirs && !*tool)
			{
				ccp end = strchr (dirs, ':');
				const uint len = end ? (uint)(end - dirs) : (uint)strlen (dirs);
				if (len)
				{
					char cand[PATH_MAX];
					snprintf (cand, sizeof (cand), "%.*s/vgmtrans", (int)len, dirs);
					if (!access (cand, X_OK))
					{
						snprintf (tool, sizeof (tool), "%s", cand);
						using_vgmtrans = true;
						break;
					}
					snprintf (cand, sizeof (cand), "%.*s/vgmtrans-cli", (int)len, dirs);
					if (!access (cand, X_OK))
					{
						snprintf (tool, sizeof (tool), "%s", cand);
						using_vgmtrans = true;
						break;
					}
				}
				dirs = end ? end + 1 : 0;
			}
		}
	}
	if (!*tool)
		return ERROR0 (ERR_WARNING,
			"wbrsar / vgmtrans not found; cannot convert %s to MIDI+SF2: %s\n",
			is_sdat || is_ext (arg, ".sdat") ? "SDAT" : "BRSAR", arg);

	// Same "<stem>.d" staging convention every other extractor in this
	// codebase uses (SARC/PAC/GFA/pass-through containers).
	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".d", false);
	else
		snprintf (dest, sizeof (dest), "%s.d", arg);

	if (!opt_overwrite && !access (dest, F_OK))
		return ERR_OK;

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXPORT %s:%s -> %s (%s)\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", is_sdat || is_ext (arg, ".sdat") ? "SDAT" : "BRSAR", arg,
			dest, using_vgmtrans ? "vgmtrans" : "wbrsar");
	if (testmode)
		return ERR_OK;

	char *const child_argv[] = { tool, (char *)arg, dest, 0 };
#ifdef __MINGW32__
	// No fork()/exec() on native Windows; _spawnv() (mingw's
	// CreateProcess-based process.h API) runs the child and blocks for its
	// exit code directly.
	intptr_t rc = _spawnv (_P_WAIT, tool, (const char *const *)child_argv);
	if (rc == -1)
		rc = _spawnvp (_P_WAIT, tool, (const char *const *)child_argv);
	if (rc != 0)
		return ERROR0 (
			ERR_WARNING, "%s failed for %s\n", using_vgmtrans ? "vgmtrans" : "wbrsar", arg);
#else
	const pid_t pid = fork ();
	if (pid < 0)
		return ERROR0 (ERR_CANT_CREATE, "fork() failed converting sound archive: %s\n", arg);
	if (pid == 0)
	{
		execv (tool, child_argv);
		execvp (tool, child_argv);
		_Exit (127); // execv failed
	}
	int status = 0;
	while (waitpid (pid, &status, 0) < 0 && errno == EINTR)
		;
	if (!WIFEXITED (status) || WEXITSTATUS (status) != 0)
		return ERROR0 (
			ERR_WARNING, "%s failed for %s\n", using_vgmtrans ? "vgmtrans" : "wbrsar", arg);
#endif
	return ERR_OK;
}

// Walk an extracted BRRES/SZS tree looking for 3D model members (MDL0 and
// friends) to export as DAE, and for BRSAR sound archives to convert to
// MIDI+SF2. ExtractFilesSZS() writes both kinds of member out as plain
// binary -- it has no notion of DAE/MIDI export -- so this is the piece
// that actually turns on "XX exports 3D models and sound banks found inside
// .brres/.szs trees" rather than just decoding them down to raw members.
static enumError export_models_tree (ccp root, uint depth)
{
	if (depth > 32)
		return ERR_FILE_TOO_BIG;
	DIR *dir = opendir (root);
	if (!dir)
		return ERR_NOT_EXISTS;
	enumError max_err = ERR_OK;
	struct dirent *de;
	while ((de = readdir (dir)))
	{
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char path[PATH_MAX];
		const int len = snprintf (path, sizeof (path), "%s/%s", root, de->d_name);
		if (len < 0 || (uint)len >= sizeof (path))
		{
			max_err = ERR_FILE_TOO_BIG;
			continue;
		}
		struct stat st;
		if (lstat (path, &st))
		{
			if (max_err < ERR_NOT_EXISTS)
				max_err = ERR_NOT_EXISTS;
			continue;
		}
		enumError err = ERR_OK;
		if (S_ISDIR (st.st_mode))
			err = export_models_tree (path, depth + 1);
		else if (S_ISREG (st.st_mode))
		{
			// decode_brfnt_if_possible()/decode_bflyt_if_possible() are also
			// called from extract_one_file(), but that call is a no-op for
			// anything reached through a nested SARC/PAC/GFA/DARC container:
			// extract_tree_complete() zeroes export_count for the duration of
			// its inner extract_tree() walk (see the comment there), and both
			// functions gate on export_count>0 the same way export_model_if_
			// possible() does. This deferred, complete-tree pass is where
			// export_count is genuinely restored, so it's the one that
			// actually reaches layout/font members bundled inside those
			// containers -- confirmed necessary on a real retail 3DS disc,
			// where DARC-bundled layout families (see extract_darc_file())
			// are exactly this case: 0 conversions without this, real output
			// with it.
			err = decode_brfnt_if_possible (path);
			const enumError cfnt_err = decode_cfnt_if_possible (path);
			if (cfnt_err != ERR_NOTHING_TO_DO && err < cfnt_err)
				err = cfnt_err;
			const enumError bflyt_err = decode_bflyt_if_possible (path);
			if (bflyt_err != ERR_NOTHING_TO_DO && err < bflyt_err)
				err = bflyt_err;
			const enumError byml_err = decode_byml_if_possible (path);
			if (byml_err != ERR_NOTHING_TO_DO && err < byml_err)
				err = byml_err;
			const enumError aamp_err = decode_aamp_if_possible (path);
			if (aamp_err != ERR_NOTHING_TO_DO && err < aamp_err)
				err = aamp_err;
			const enumError kcl_err = export_kcl_glb_if_possible (path);
			if (kcl_err != ERR_NOTHING_TO_DO && err < kcl_err)
				err = kcl_err;
			const enumError img_err = decode_image_if_possible (path);
			if (img_err != ERR_NOTHING_TO_DO && err < img_err)
				err = img_err;
			const enumError model_err = export_model_if_possible (path);
			if (model_err != ERR_NOTHING_TO_DO && err < model_err)
				err = model_err;
			const enumError brsar_err = convert_brsar_if_possible (path);
			if (brsar_err != ERR_NOTHING_TO_DO && err < brsar_err)
				err = brsar_err;
			// extract_hsf_file() deferred its own conversion here (see
			// g_in_model_deferred_pass) so it can run after SetDAETextureSearchRoot()
			// has actually indexed the textures HSF itself decodes as a side
			// effect of the very same DecodeHSF() call -- g_in_model_deferred_pass
			// is back off by now, so this call does the real work.
			const enumError hsf_err = extract_hsf_file (path, 0, depth);
			if (hsf_err != ERR_NOTHING_TO_DO && err < hsf_err)
				err = hsf_err;
			// Same deferral, for the same reason: .mod resolves its texture
			// names against the sibling PNGs this pass can finally see.
			const enumError mod_err = extract_mod_file (path, 0, depth);
			if (mod_err != ERR_NOTHING_TO_DO && err < mod_err)
				err = mod_err;
		}
		if (err != ERR_NOTHING_TO_DO)
		{
			if (err > ERR_WARNING)
				err = ERR_WARNING;
			if (max_err < err)
				max_err = err;
		}
	}
	closedir (dir);
	return max_err;
}

static inline u16 bft_rb16 (const u8 *p)
{
	return (u16)p[0] << 8 | p[1];
}
static inline u32 bft_rb32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
static inline s32 bft_rbs32 (const u8 *p)
{
	return (s32)bft_rb32 (p);
}
// Resolves a self-relative offset stored at ADDR, same convention as
// lib-bfres.c's own REL() macro (Wii U BFRES offsets are relative to the
// field that holds them).
#define BFT_REL(base, addr) ((size_t)(addr) + (size_t)bft_rbs32 ((base) + (addr)))

static const char *bft_rel_string (const u8 *d, size_t size, size_t at)
{
	if (at + 4 > size)
		return NULL;
	const size_t p = BFT_REL (d, at);
	if (p >= size)
		return NULL;
	for (size_t q = p; q < size; q++)
		if (!d[q])
			return (const char *)(d + p);
	return NULL;
}
