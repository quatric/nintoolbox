// Extract every ALAR member and validate the complete directory before writing.
// DSCP-wrapped archives occur inside retail Jump Ultimate Stars containers.
static enumError extract_alar_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size >= 4 && !memcmp (raw, "DSCP", 4) && raw_size <= UINT_MAX)
	{
		u8 *decoded = 0;
		uint decoded_size = 0;
		enumError err = DecodeLZ10LZ11 (&decoded, &decoded_size, raw, raw_size);
		FREE (raw);
		if (err)
			return ERR_INVALID_DATA;
		raw = decoded;
		raw_size = decoded_size;
	}
	if (raw_size < 4 || memcmp (raw, "ALAR", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size < 16 || raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}
	const uint count = le16 (raw + 6);
	alar_entry_t *entries = count ? CALLOC (count, sizeof (*entries)) : 0;
	if (!entries)
	{
		FREE (raw);
		return count ? ERR_OUT_OF_MEMORY : ERR_INVALID_DATA;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	enumError err = ERR_OK;
	for (uint i = 0; i < count; i++)
	{
		err = ReadALAREntry (entries + i, raw, raw_size, i);
		if (err)
			break;
		if (entries[i].name && !valid_sarc_path (entries[i].name))
		{
			err = ERR_INVALID_DATA;
			break;
		}
		const size_t name_len = entries[i].name ? strlen (entries[i].name) : 24;
		if (strlen (dest) + 1 + (basedir ? strlen (basedir) : 0) + name_len >= PATH_MAX)
		{
			err = ERR_INVALID_DATA;
			break;
		}
	}
	if (!err && (verbose >= 0 || testmode))
		fprintf (stdlog, "%s%sEXTRACT ALAR:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);
	for (uint i = 0; !err && !testmode && i < count; i++)
	{
		char generated[32], path[PATH_MAX];
		snprintf (generated, sizeof (generated), "file%u.bin", i);
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "",
			entries[i].name ? entries[i].name : generated);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && entries[i].size
			&& fwrite (entries[i].data, 1, entries[i].size, F.f) != entries[i].size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing ALAR member failed: %s\n", path);
		ResetFile (&F, opt_preserve);
	}
	FREE (entries);
	FREE (raw);
	if (!err && !testmode && (opt_recurse < 0 || depth < (uint)opt_recurse))
		err = extract_tree_complete (dest, depth + 1);
	return err;
}
