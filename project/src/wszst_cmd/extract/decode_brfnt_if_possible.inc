// Decode a Wii bitmap font (BRFNT/BRFNA, container magic "RFNT"/"RFNA") to
// PNG sheet(s) during "wszst XX". The pixel decode itself already exists --
// AssignIMG()'s TGLP branch in lib-image-load.c fully understands it (see the
// comment above extract_cfnt_manifest()) -- but nothing in the XX/EXTRACT
// tree walk ever called it; only an explicit `wimgt DECODE some.brfnt` did,
// so a real font found inside an extracted disc was left as an undecoded
// raw member. Mirrors wimgt.c's own cmd_decode(): TGLP glyph sheets are
// independent atlases, not a mip chain, so each info_n_image record gets
// its own PNG.
static enumError decode_brfnt_if_possible (ccp arg)
{
	// The sheet exporter is useful for an explicit wimgt inspection, but game
	// staging has no corresponding lossless sheet injector. Do not add PNGs
	// here: CREATE would otherwise remove an edited sheet and retain the old
	// BRFNT member without reporting that the edit was dropped.
	return ERR_NOTHING_TO_DO;

	u8 magic[4];
	FILE *probe = fopen (arg, "rb");
	if (!probe)
		return ERR_NOT_EXISTS;
	const size_t n_magic = fread (magic, 1, sizeof (magic), probe);
	fclose (probe);
	if (n_magic != sizeof (magic) || (memcmp (magic, "RFNT", 4) && memcmp (magic, "RFNA", 4)))
		return ERR_NOTHING_TO_DO;

	Image_t img;
	enumError max_err = LoadIMG (&img, true, arg, 0, false, true, false);
	if (max_err)
		return ERR_NOTHING_TO_DO;

	char dest_base[PATH_MAX];
	if (opt_dest)
		SubstDest (dest_base, sizeof (dest_base), arg, opt_dest, 0, ".png", false);
	else
		snprintf (dest_base, sizeof (dest_base), "%s.png", arg);

	const uint record_images
		= img.info_fform == FF_UNKNOWN && img.info_n_image > 1 ? img.info_n_image : 1;

	if (record_images <= 1)
	{
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE BRFNT:%s -> PNG:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, dest_base);
		if (!testmode)
		{
			const enumError s_err = SavePNG (&img, true, 0, dest_base, 0, 0, false, 0);
			if (s_err <= ERR_WARNING && max_err < s_err)
				max_err = s_err;
		}
		ResetIMG (&img);
		return max_err;
	}

	ResetIMG (&img);
	for (uint image_index = 0; image_index < record_images; image_index++)
	{
		const enumError load_err = LoadIMG (&img, true, arg, image_index, false, true, false);
		if (load_err)
			continue;

		char dest[PATH_MAX];
		ccp ext = strrchr (dest_base, '.');
		if (ext)
			snprintf (dest, sizeof (dest), "%.*s.sheet%03u%s", (int)(ext - dest_base), dest_base,
				image_index, ext);
		else
			snprintf (dest, sizeof (dest), "%s.sheet%03u.png", dest_base, image_index);

		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE BRFNT:%s[%u] -> PNG:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, image_index, dest);
		if (!testmode)
		{
			const enumError sheet_err = SavePNG (&img, true, 0, dest, 0, 0, false, 0);
			if (sheet_err <= ERR_WARNING && max_err < sheet_err)
				max_err = sheet_err;
		}
		ResetIMG (&img);
	}
	return max_err;
}

// Decode a 3DS/Wii U bitmap font (BCFNT/BFFNT) to PNG sheet(s) during
// "wszst XX". AssignIMG()'s NFMT_BCFNT branch in lib-image-load.c handles the
// pixel decode. Mirrors decode_brfnt_if_possible() sheet-iteration logic:
// each TGLP glyph sheet is an independent atlas, so info_n_image > 1 gets
// one PNG per sheet.
static enumError decode_cfnt_if_possible (ccp arg)
{
	if (!is_ext (arg, ".bffnt") && !is_ext (arg, ".bcfnt") && !is_ext (arg, ".ffnt")
		&& !is_ext (arg, ".cfnt"))
		return ERR_NOTHING_TO_DO;

	u8 magic[4];
	FILE *probe = fopen (arg, "rb");
	if (!probe)
		return ERR_NOT_EXISTS;
	const size_t n_magic = fread (magic, 1, sizeof (magic), probe);
	fclose (probe);
	if (n_magic != sizeof (magic) || (memcmp (magic, "CFNT", 4) && memcmp (magic, "FFNT", 4)))
		return ERR_NOTHING_TO_DO;

	Image_t img;
	enumError max_err = LoadIMG (&img, true, arg, 0, false, true, false);
	if (max_err)
		return ERR_NOTHING_TO_DO;

	char dest_base[PATH_MAX];
	if (opt_dest)
		SubstDest (dest_base, sizeof (dest_base), arg, opt_dest, 0, ".png", false);
	else
		snprintf (dest_base, sizeof (dest_base), "%s.png", arg);

	const uint record_images
		= img.info_fform == FF_UNKNOWN && img.info_n_image > 1 ? img.info_n_image : 1;

	if (record_images <= 1)
	{
		ccp tag = !memcmp (magic, "CFNT", 4) ? "BCFNT" : "BFFNT";
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE %s:%s -> PNG:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", tag, arg, dest_base);
		if (!testmode)
		{
			const enumError s_err = SavePNG (&img, true, 0, dest_base, 0, 0, false, 0);
			if (s_err <= ERR_WARNING && max_err < s_err)
				max_err = s_err;
		}
		ResetIMG (&img);
		return max_err;
	}

	ResetIMG (&img);
	for (uint image_index = 0; image_index < record_images; image_index++)
	{
		const enumError load_err = LoadIMG (&img, true, arg, image_index, false, true, false);
		if (load_err)
			continue;

		char dest[PATH_MAX];
		ccp ext = strrchr (dest_base, '.');
		if (ext)
			snprintf (dest, sizeof (dest), "%.*s.sheet%03u%s", (int)(ext - dest_base), dest_base,
				image_index, ext);
		else
			snprintf (dest, sizeof (dest), "%s.sheet%03u.png", dest_base, image_index);

		ccp tag = !memcmp (magic, "CFNT", 4) ? "BCFNT" : "BFFNT";
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE %s:%s[%u] -> PNG:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", tag, arg, image_index, dest);
		if (!testmode)
		{
			const enumError sheet_err = SavePNG (&img, true, 0, dest, 0, 0, false, 0);
			if (sheet_err <= ERR_WARNING && max_err < sheet_err)
				max_err = sheet_err;
		}
		ResetIMG (&img);
	}
	return max_err;
}

// Convert a BFLYT/BCLYT/BRLYT/BRLAN layout or animation to XML (.xml)
// during "wszst XX", mirroring what `wszst TEXT` already does for a single
// file (see cmd_convert()'s "Handle layout formats" branch a few hundred
// lines below). A layout/anim member found inside an extracted tree (e.g.
// a game's LZ-compressed romfs .bin -- already plain by the time this runs,
// decompress_nintendo_file() ran first) was previously left as an undecoded
// binary blob; nothing in the XX/EXTRACT tree walk called ScanBFLYT()/
// SaveXMLBFLYT(). Only runs when export_count>0 (XX aliases to XEXPORT),
// matching the gate used by decode_brfnt_if_possible()/export_model_if_possible().
static enumError decode_bflyt_if_possible (ccp arg)
{
	if (export_count <= 0)
		return ERR_NOTHING_TO_DO;

	u8 magic[4];
	FILE *probe = fopen (arg, "rb");
	if (!probe)
		return ERR_NOT_EXISTS;
	const size_t n_magic = fread (magic, 1, sizeof (magic), probe);
	fclose (probe);

	const bool is_lyt_magic = n_magic == sizeof (magic)
		&& (!memcmp (magic, "FLYT", 4) || !memcmp (magic, "CLYT", 4) || !memcmp (magic, "FLAN", 4)
			|| !memcmp (magic, "CLAN", 4) || !memcmp (magic, "RLYT", 4)
			|| !memcmp (magic, "RLAN", 4));
	const bool is_lyt_ext = is_ext (arg, ".bflyt") || is_ext (arg, ".bclyt")
		|| is_ext (arg, ".brlyt") || is_ext (arg, ".brlan") || is_ext (arg, ".bflan")
		|| is_ext (arg, ".bclan") || is_ext (arg, ".flyt") || is_ext (arg, ".clyt")
		|| is_ext (arg, ".flan") || is_ext (arg, ".clan");

	if (!is_lyt_magic && !is_lyt_ext)
		return ERR_NOTHING_TO_DO;

	raw_data_t raw;
	InitializeRawData (&raw);
	enumError err = LoadRawData (&raw, false, arg, 0, false, 0);
	if (err > ERR_WARNING)
	{
		ResetRawData (&raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw.fform != FF_BFLYT && raw.fform != FF_BCLYT && raw.fform != FF_BRLYT
		&& raw.fform != FF_BRLAN)
	{
		ResetRawData (&raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".xml", false);
	else
		snprintf (dest, sizeof (dest), "%s.xml", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sCREATE/TEXT %s:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", GetNameFF (raw.fform, 0), arg, dest);
	if (testmode)
	{
		ResetRawData (&raw);
		return ERR_OK;
	}

	bflyt_t bflyt;
	InitializeBFLYT (&bflyt);
	err = ScanBFLYT (&bflyt, false, raw.data, raw.data_size);
	if (!err)
		err = SaveXMLBFLYT (&bflyt, dest, true);
	ResetBFLYT (&bflyt);
	ResetRawData (&raw);
	return err ? err : ERR_OK;
}

// Export a KCL collision file to a viewable .kcl.glb companion during
// "wszst XX", alongside the existing .obj text-editing round trip (which
// SaveTextKCL/ScanTextKCL still own -- that pairing is unrelated to this
// export and is left untouched). KCL has no magic signature, so detection
// is by extension only, same as decode_bflyt_if_possible()'s is_lyt_ext path.
static enumError export_kcl_glb_if_possible (ccp arg)
{
	if (export_count <= 0)
		return ERR_NOTHING_TO_DO;
	if (!is_ext (arg, ".kcl"))
		return ERR_NOTHING_TO_DO;

	raw_data_t raw;
	InitializeRawData (&raw);
	enumError err = LoadRawData (&raw, false, arg, 0, false, 0);
	if (err > ERR_WARNING || raw.fform != FF_KCL)
	{
		ResetRawData (&raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".glb", false);
	else
		snprintf (dest, sizeof (dest), "%s.glb", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sCREATE/GLB %s:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", GetNameFF (raw.fform, 0), arg, dest);
	if (testmode)
	{
		ResetRawData (&raw);
		return ERR_OK;
	}

	kcl_t kcl;
	InitializeKCL (&kcl);
	kcl.fname = arg;
	err = ScanKCL (&kcl, false, raw.data, raw.data_size, false, global_check_mode);
	if (!err)
		err = SaveGlbKCL (&kcl, dest, true);
	kcl.fname = 0;
	ResetKCL (&kcl);
	ResetRawData (&raw);
	return err ? err : ERR_OK;
}

static enumError decode_byml_if_possible (ccp arg)
{
	if (export_count <= 0)
		return ERR_NOTHING_TO_DO;
	if (!is_ext (arg, ".byml") && !is_ext (arg, ".byaml") && !is_ext (arg, ".bin")
		&& !is_ext (arg, ".bgyml") && !is_ext (arg, ".bgdata") && !is_ext (arg, ".sbgdata")
		&& !is_ext (arg, ".bquestpack") && !is_ext (arg, ".sbquestpack") && !is_ext (arg, ".sbyml")
		&& !is_ext (arg, ".mubin") && !is_ext (arg, ".smubin") && !is_ext (arg, ".baischedule")
		&& !is_ext (arg, ".sbaischedule") && !is_ext (arg, ".baniminfo")
		&& !is_ext (arg, ".sbaniminfo") && !is_ext (arg, ".bgsvdata")
		&& !is_ext (arg, ".sbgsvdata"))
		return ERR_NOTHING_TO_DO;

	u8 *data = 0;
	size_t size = 0;
	// LoadBYMLData() unwraps Yaz0 transparently, so BotW-style S-prefixed
	// containers (.sbyml, .smubin, ...) decode here just like plain BYML.
	// NintenTools.Byaml does the same Yaz0-first step in its editor.
	enumError err = LoadBYMLData (arg, &data, &size);
	if (err)
	{
		FREE (data);
		return ERR_NOTHING_TO_DO;
	}
	if (size > UINT_MAX)
	{
		FREE (data);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".yml", false);
	else
		snprintf (dest, sizeof (dest), "%s.yml", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE BYML:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);
	if (testmode)
	{
		FREE (data);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (!F.f)
	{
		FREE (data);
		return err;
	}
	ccp ext = strrchr (dest, '.');
	if (ext && !strcasecmp (ext, ".xml"))
		err = DecodeBYML_XML (F.f, data, size);
	else if (ext && !strcasecmp (ext, ".json"))
		err = DecodeBYML_JSON (F.f, data, size);
	else
		err = DecodeBYML_YAML (F.f, data, size);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static enumError decode_msbt_if_possible (ccp arg)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOT_EXISTS;
	u8 head[32];
	size_t got = fread (head, 1, sizeof (head), f);
	fclose (f);
	if (got < 32)
		return ERR_NOTHING_TO_DO;

	bool is_msbt = IsMSBT (head, (uint)got);
	bool is_msbp = IsMSBP (head, (uint)got);
	bool is_msbf = IsMSBF (head, (uint)got);
	if (!is_msbt && !is_msbp && !is_msbf)
		return ERR_NOTHING_TO_DO;

	u8 *data = 0;
	size_t size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &data, &size, 0, 0, 0, false);
	if (err || !data)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0,
			is_msbt		  ? ".tmsbt"
				: is_msbp ? ".tmsbp"
						  : ".tmsbf",
			false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE %s:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "",
			is_msbt		  ? "MSBT"
				: is_msbp ? "MSBP"
						  : "MSBF",
			arg, dest);

	if (testmode)
	{
		FREE (data);
		return ERR_OK;
	}

	if (is_msbt)
	{
		msbt_file_t msbt;
		err = ScanMSBT (&msbt, data, (uint)size, arg);
		if (!err)
			err = SaveTextMSBT (&msbt, dest);
		ResetMSBT (&msbt);
	}
	else if (is_msbp)
	{
		msbp_file_t msbp;
		err = ScanMSBP (&msbp, data, (uint)size, arg);
		if (!err)
			err = SaveTextMSBP (&msbp, dest);
		ResetMSBP (&msbp);
	}
	else if (is_msbf)
	{
		msbf_file_t msbf;
		err = ScanMSBF (&msbf, data, (uint)size, arg);
		if (!err)
			err = SaveTextMSBF (&msbf, dest);
		ResetMSBF (&msbf);
	}

	FREE (data);
	return err;
}
