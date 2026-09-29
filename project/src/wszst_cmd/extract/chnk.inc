// NOTE: Opoona .mol/.mot manifest-dump wrappers (extract_opoona_mol_manifest/
// extract_opoona_mot_manifest, following the extract_gf1motion_manifest
// pattern above) are intentionally not defined here. Wiring them into the
// `wszst xx` dispatch chain requires registering them in formats.inc, and
// this session's standing instructions are to leave that file untouched
// (it carries an unrelated, concurrent uncommitted change from someone
// else's work in this tree). FILETYPE recognition (IsOpoonaMOL/IsOpoonaMOT
// in lib-opoona.c, wired via lib-file.c and file-type.c) and the
// DecodeOpoonaMOL_Text/DecodeOpoonaMOT_Text dumpers themselves are fully
// implemented and ready to wire in exactly this shape once formats.inc can
// be touched again.

// Monster 4x4: Stunt Racer "CHNK" chunked container, shared verbatim across
// .bnk/.d4l/.d4c/.gfx/.mec/.pmu/.pak/.ppx/.an4. Only the top-level chunk
// table is dumped (see lib-monster4x4.h for what is not decoded).
static enumError extract_chnk_manifest (ccp arg)
{
	static const ccp exts[]
		= { ".bnk", ".d4l", ".d4c", ".gfx", ".mec", ".pmu", ".pak", ".ppx", ".an4", 0 };
	bool match = false;
	for (int i = 0; exts[i]; i++)
		if (is_ext (arg, exts[i]))
		{
			match = true;
			break;
		}
	if (!match)
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsCHNK (raw, raw_size, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE CHNK:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f)
		err = DecodeCHNK_Text (F.f, raw, raw_size, raw_size);
	ResetFile (&F, opt_preserve);
	FREE (raw);
	return err;
}
