// True when PATH is a sibling extraction output: a directory named "<stem>.d"
// whose "<stem>" is a regular file next to it.  The native extractors write
// every decoded archive into a "<source>.d" folder beside its source, and
// those artifacts must not be re-fed into the XX pipeline (a 0-byte
// ".BMG.header" inside a ".bmg.d" was being resubmitted as a BMG, tripping
// the BMG text writer).  Pass-through stage dirs (wit's "<game>.d") have no
// sibling regular file, so this never skips the tree we are told to walk.
static bool is_extractor_output_dir (ccp path)
{
	const uint n = strlen (path);
	if (n < 3 || path[n - 1] != 'd' || path[n - 2] != '.')
		return false;

	char stem[PATH_MAX];
	snprintf (stem, sizeof (stem), "%.*s", (int)(n - 2), path);
	struct stat st;
	if (lstat (stem, &st) == 0 && S_ISREG (st.st_mode))
		return true;

	char setup[PATH_MAX];
	snprintf (setup, sizeof (setup), "%s/%s", path, SZS_SETUP_FILE);
	if (lstat (setup, &st) == 0 && S_ISREG (st.st_mode))
		return true;

	// U8/RARC/PACK destinations use the source stem (foo.arc -> foo.d), not
	// the complete source filename (foo.arc.d). Recognize any regular sibling
	// named "<stem>.<extension>" so a live parent readdir never feeds the
	// output directory a second time after extract_one_file() already peeled
	// it explicitly above.
	// Also strip intermediate extensions successively from base to recognize
	// output directories whose intermediate decompressed archive was unlinked
	// (e.g. m2dres_narc.blz -> m2dres_narc.bin -> m2dres_narc.bin.d).
	char parent[PATH_MAX], base[PATH_MAX];
	ccp slash = strrchr (stem, '/');
	if (slash)
	{
		snprintf (parent, sizeof (parent), "%.*s", (int)(slash - stem), stem);
		snprintf (base, sizeof (base), "%s", slash + 1);
	}
	else
	{
		snprintf (parent, sizeof (parent), ".");
		snprintf (base, sizeof (base), "%s", stem);
	}
	DIR *dir = opendir (parent);
	if (!dir)
		return false;
	bool found = false;
	struct dirent *de;
	char prefix[PATH_MAX];
	snprintf (prefix, sizeof (prefix), "%s", base);
	while (!found && *prefix)
	{
		const size_t prefix_len = strlen (prefix);
		rewinddir (dir);
		while (!found && (de = readdir (dir)))
		{
			if (strncmp (de->d_name, prefix, prefix_len) || de->d_name[prefix_len] != '.'
				|| !strcmp (de->d_name + prefix_len, ".d"))
				continue;
			char candidate[PATH_MAX];
			snprintf (candidate, sizeof (candidate), "%s/%s", parent, de->d_name);
			found = !lstat (candidate, &st) && S_ISREG (st.st_mode);
		}
		char *dot = strrchr (prefix, '.');
		if (dot)
			*dot = '\0';
		else
			break;
	}
	closedir (dir);
	return found;
}

// Recurse into a directory produced by an external unpacker, peeling every
// regular file found below it exactly like a top-level XX argument.  Each
// decoded file lands beside its own source in the staged tree so the project
// tree stays deterministic for rebuilds.  Sub-directories created by the
// native extractors while unpacking ("<source>.d") are skipped so the walk
// only ever sees the tree the external tool produced.
static enumError extract_tree (ccp root, uint depth)
{
	if (depth > 32)
		return ERR_FILE_TOO_BIG;
	DIR *dir = opendir (root);
	if (!dir)
		return ERR_NOT_EXISTS;
	enumError max_err = ERR_OK;
	struct dirent *de;
	ccp saved_dest = opt_dest;
	opt_dest = 0; // enforce tree-local destinations during tree walk
	while ((de = readdir (dir)))
	{
		if (de->d_name[0] == '.')
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
		{
			if (!is_extractor_output_dir (path)
				&& (opt_with_update_part || strcasecmp (de->d_name, "UPDATE")))
			{
				// Count nested containers, not ordinary filesystem directories.
				err = extract_tree (path, depth);
			}
		}
		else if (S_ISREG (st.st_mode))
		{
			err = extract_one_file (path, 0, depth + 1);
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
	opt_dest = saved_dest;
	// Pass-through extraction can create nested native .d trees while this
	// walk is running. Normalize their generated timestamps only after the
	// whole subtree has been peeled, so the first CREATE remains incremental.
	normalize_ds_nested_mtimes (root);
	return max_err;
}

static enumError cmd_extract (enumCommands mode)
{
	ccp basedir = GetOptBasedir ();
	// Every decode-oriented alias (XDECODE/XEXPORT a.k.a. XX/XALL) implies
	// --auto: without it, a raw Nintendo codec stream (LZ10/LZ11/etc.) found
	// while walking a plain directory argument gets decoded and then
	// immediately discarded again as an "unclaimed raw dump" (see the
	// depth>0 discard in extract_one_file_inner() -- ordinary directory
	// recursion also increments depth, so that check can't tell "reached by
	// unwrapping a real nested container" from "just a loose file a few
	// folders deep in an unpacked disc"). --auto's own documented purpose is
	// to keep exactly this class of output, so make it the default for
	// these modes instead of requiring users to remember the flag every time.
	if (mode == CMD_XDECODE)
	{
		RegisterOptionByIndex (&InfoUI_wszst, OPT_DECODE, 1, false);
		opt_decode = true;
		RegisterOptionByIndex (&InfoUI_wszst, OPT_AUTO, 1, false);
	}
	else if (mode == CMD_XEXPORT)
	{
		RegisterOptionByIndex (&InfoUI_wszst, OPT_DECODE, 1, false);
		opt_decode = true;
		RegisterOptionByIndex (&InfoUI_wszst, OPT_EXPORT, 1, false);
		export_count++;
		RegisterOptionByIndex (&InfoUI_wszst, OPT_AUTO, 1, false);
	}
	else if (mode == CMD_XALL)
	{
		RegisterOptionByIndex (&InfoUI_wszst, OPT_ALL, 1, false);
		opt_recurse = INT_MAX;
		opt_decode = true;
		opt_mipmaps = 1;
		RegisterOptionByIndex (&InfoUI_wszst, OPT_AUTO, 1, false);
	}
	else if (mode == CMD_XCOMMON)
	{
		opt_recurse = INT_MAX;
		opt_decode = false;
		basedir = "common/";
	}

	if (opt_dest && !*opt_dest)
		opt_dest = 0;
	opt_mkdir = true;

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);
	enumError max_err = ERR_OK;

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		// XX historically recursed only into directories produced by an external
		// pass-through extractor.  Treat a directory supplied on the command line
		// the same way, so `wszst XX path/to/tree` peels every regular file below
		// it while still skipping sibling "<archive>.d" output directories.
		enumError err;
		if (IsDirectory (arg, false) && export_count > 0)
		{
			err = extract_tree_complete (arg, 0);
		}
		else
			err = IsDirectory (arg, false) ? extract_tree (arg, 0)
										   : extract_one_file (arg, basedir, 0);
		if (max_err < err)
			max_err = err;
	}

	ResetStringField (&plist);
	return max_err;
}
