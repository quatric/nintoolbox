// Extract the 3DS/NW4C DARC tree using directory subtree end indices.
// The parser validates parents and member ranges; this pass validates paths
// before writing any files, including archives with a second root alias.
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
		return err;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT DARC:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, darc.n_entries, dest);

	struct darc_extract_frame_t
	{
		uint end;
		size_t path_len;
	};
	struct darc_extract_frame_t *stack = CALLOC (darc.n_entries, sizeof (*stack));
	if (!stack)
		err = ERR_OUT_OF_MEMORY;

	// Validate the complete tree and assembled output paths before creating files.
	for (uint pass = 0; !err && pass < 2; pass++)
	{
		uint sp = 0;
		stack[0].end = darc.n_entries;
		stack[0].path_len = 0;
		char rel[PATH_MAX];
		for (uint i = 1; !err && i < darc.n_entries; i++)
		{
			while (sp && i >= stack[sp].end)
				sp--;
			const darc_entry_t *e = darc.entries + i;
			const size_t parent_len = stack[sp].path_len;
			rel[parent_len] = 0;
			// Retail archives include a second root entry named ".".
			if (e->is_dir && i == 1 && e->parent_or_offset == 0
				&& (!*e->name || !strcmp (e->name, ".")))
			{
				stack[++sp].end = e->end_or_size;
				stack[sp].path_len = parent_len;
				continue;
			}
			const int added = snprintf (rel + parent_len, sizeof (rel) - parent_len, "%s%s",
				parent_len ? "/" : "", e->name);
			if (!*e->name || !strcmp (e->name, ".") || strchr (e->name, '/') || added < 0
				|| (size_t)added >= sizeof (rel) - parent_len || !valid_sarc_path (rel))
			{
				err = ERROR0 (ERR_INVALID_DATA, "Invalid DARC entry path: %s\n", e->name);
				break;
			}
			if (e->is_dir)
			{
				stack[++sp].end = e->end_or_size;
				stack[sp].path_len = parent_len + added;
				continue;
			}
			char path[PATH_MAX];
			const int path_len
				= snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", rel);
			if (path_len < 0 || (size_t)path_len >= sizeof (path))
			{
				err = ERROR0 (ERR_INVALID_DATA, "DARC output path is too long: %s\n", rel);
				break;
			}
			if (!pass || testmode)
				continue;
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
	}
	FREE (stack);

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
