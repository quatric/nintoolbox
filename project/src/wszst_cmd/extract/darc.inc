// Extract a DARC archive ("darc" magic) -- the 3DS/NW4C counterpart of SARC,
// found bundling a game's layout family into one romfs file (see the format
// comment above darc_t in lib-nintendo.h). Unlike PAC/GFA's flat member
// list, DARC's tree can nest arbitrarily deep (parent-index/end-index per
// entry), so this walks it with an explicit directory stack rather than
// GFA's single "current subdir" variable -- a nested dir ending resets to
// whatever directory is now on top of the stack, not to the root.
static enumError extract_darc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".darc") && !is_ext (arg, ".arc") && !is_ext (arg, ".bin")
		&& !is_ext (arg, ".bcma"))
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
	if (raw_size < 4 || memcmp (raw, "darc", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	darc_t darc;
	err = ScanDARC (&darc, raw, (uint)raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT DARC:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, darc.n_entries, dest);

	// stack[k].path is the assembled relative path of the directory at
	// depth k (root = ""); stack[k].end is that directory's end-index.
	enum
	{
		MAX_DARC_DEPTH = 64
	};
	struct
	{
		char path[PATH_MAX];
		uint end;
	} stack[MAX_DARC_DEPTH];
	uint sp = 0;
	stack[0].path[0] = 0;
	stack[0].end = darc.n_entries;

	for (uint i = 1; !err && i < darc.n_entries; i++)
	{
		while (sp > 0 && i >= stack[sp].end)
			sp--;
		const darc_entry_t *e = darc.entries + i;

		if (e->is_dir)
		{
			// Entry 1 is commonly a "." alias of the root (per GBATEK); real
			// subfolders never have an empty or "." name.
			if (!*e->name || !strcmp (e->name, "."))
				continue;
			if (sp + 1 >= MAX_DARC_DEPTH || e->end_or_size > darc.n_entries)
			{
				err = ERROR0 (ERR_INVALID_DATA, "DARC nesting too deep or corrupt: %s\n", arg);
				break;
			}
			char rel[PATH_MAX];
			if (*stack[sp].path)
				snprintf (rel, sizeof (rel), "%s/%s", stack[sp].path, e->name);
			else
				snprintf (rel, sizeof (rel), "%s", e->name);
			if (!valid_sarc_path (rel))
			{
				err = ERROR0 (ERR_INVALID_DATA, "Unsafe DARC entry path: %s\n", rel);
				break;
			}
			sp++;
			snprintf (stack[sp].path, sizeof (stack[sp].path), "%s", rel);
			stack[sp].end = e->end_or_size;
			continue;
		}

		char rel[PATH_MAX];
		if (*stack[sp].path)
			snprintf (rel, sizeof (rel), "%s/%s", stack[sp].path, e->name);
		else
			snprintf (rel, sizeof (rel), "%s", e->name);
		if (!valid_sarc_path (rel))
		{
			err = ERROR0 (ERR_INVALID_DATA, "Unsafe DARC entry path: %s\n", rel);
			break;
		}
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", rel);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->end_or_size
			&& fwrite (raw + e->parent_or_offset, 1, e->end_or_size, F.f) != e->end_or_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->end_or_size, path);
		const enumError close_err = ResetFile (&F, opt_preserve);
		if (close_err > err)
			err = close_err;
	}

	ResetDARC (&darc);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
