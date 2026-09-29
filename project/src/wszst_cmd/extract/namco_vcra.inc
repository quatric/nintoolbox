static enumError extract_namco_vcra_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size < 16 || raw_size > UINT_MAX || memcmp (raw, "VCRA", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint n_entries = le32 (raw + 4), archive_size = le32 (raw + 8);
	const bool has_crc = le32 (raw + 12) != 0;
	const uint table = has_crc ? 12 : 64;
	const uint entry_size = has_crc ? 44 : 64;
	const uint name_size = has_crc ? 32 : 56;
	const uint name_offset = has_crc ? 12 : 8;
	if (!n_entries || n_entries > (raw_size - table) / entry_size || archive_size != raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint table_end = table + n_entries * entry_size;
	for (uint i = 0; i < n_entries; i++)
	{
		const u8 *entry = raw + table + i * entry_size;
		const uint off = le32 (entry), size = le32 (entry + 4);
		if (off < table_end || off > raw_size || size > raw_size - off)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT VCRA:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);
	enumError err = ERR_OK;
	for (uint i = 0; !err && i < n_entries; i++)
	{
		const u8 *entry = raw + table + i * entry_size;
		char name[57];
		memcpy (name, entry + name_offset, name_size);
		name[name_size] = 0;
		if (!valid_sarc_path (name))
			snprintf (name, sizeof (name), "file_%04u.bin", i);
		if (testmode)
			continue;
		const uint off = le32 (entry), size = le32 (entry + 4);
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (raw + off, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
		ResetFile (&F, opt_preserve);
	}
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
