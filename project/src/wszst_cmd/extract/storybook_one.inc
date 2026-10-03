static enumError extract_storybook_one_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".one"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (raw_size < 16 || raw_size > UINT_MAX)
		goto cleanup;
	const uint n_entries = be32 (raw), table = be32 (raw + 4), data = be32 (raw + 8),
			   marker = be32 (raw + 12);
	if (!n_entries || n_entries > (raw_size - 16) / 48 || table != 16
		|| (marker != 0 && marker != UINT_MAX) || data < table + n_entries * 48 || data > raw_size)
		goto cleanup;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT Sonic Storybook ONE:%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n_entries, dest);
	err = ERR_OK;
	for (uint i = 0; !err && i < n_entries; i++)
	{
		const u8 *entry = raw + table + i * 48;
		char name[33];
		memcpy (name, entry, 32);
		name[32] = 0;
		const uint offset = be32 (entry + 36), size = be32 (entry + 40),
				   out_size = be32 (entry + 44);
		if (!valid_sarc_path (name) || !*name || offset < data || offset > raw_size
			|| size > raw_size - offset || !out_size
			|| (opt_max_file_size && out_size > opt_max_file_size))
		{
			err = ERR_INVALID_DATA;
			break;
		}
		if (testmode)
			continue;
		u8 *out = MALLOC (out_size);
		if (!decode_storybook_prs (out, out_size, raw + offset, size))
		{
			FREE (out);
			err = ERR_INVALID_DATA;
			break;
		}
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (out, 1, out_size, F.f) != out_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", out_size, path);
		const enumError close_err = ResetFile (&F, opt_preserve);
		if (close_err > err)
			err = close_err;
		FREE (out);
	}
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
cleanup:
	FREE (raw);
	return err;
}
