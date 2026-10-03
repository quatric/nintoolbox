// Layton PLZ files wrap PCK2 in Nintendo LZ10; raw PCK2 has its magic at +8.
static enumError extract_pck2_file (ccp arg, ccp basedir, uint depth)
{
	u8 header[12];
	FILE *in = fopen (arg, "rb");
	const size_t got = in ? fread (header, 1, sizeof (header), in) : 0;
	if (in)
		fclose (in);
	const bool wrapped = !(got >= 12 && !memcmp (header + 8, "PCK2", 4)) && is_ext (arg, ".plz")
		&& got >= 4 && (header[0] == 0x10 || header[0] == 0x11);
	if (!wrapped && (got < 12 || memcmp (header + 8, "PCK2", 4)))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, UINT_MAX, 0, 0, false);
	if (err)
		return err;
	if (wrapped)
	{
		u8 *decoded = 0;
		uint decoded_size = 0;
		err = DecodeLZ10LZ11 (&decoded, &decoded_size, raw, (uint)raw_size);
		FREE (raw);
		if (err)
			return err;
		raw = decoded;
		raw_size = decoded_size;
		if (raw_size < 12 || memcmp (raw + 8, "PCK2", 4))
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}
	pck2_entry_t *entries = 0;
	uint count = 0;
	err = ScanPCK2 (&entries, &count, raw, (uint)raw_size);
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	for (uint i = 0; !err && i < count; i++)
		if (!valid_sarc_path (entries[i].name)
			|| strlen (dest) + 1 + (basedir ? strlen (basedir) : 0) + strlen (entries[i].name)
				>= PATH_MAX)
			err = ERR_INVALID_DATA;
	if (!err && (verbose >= 0 || testmode))
		fprintf (stdlog, "%sEXTRACT PCK2:%s (%u files) -> %s/\n", testmode ? "WOULD " : "", arg,
			count, dest);
	for (uint i = 0; !err && !testmode && i < count; i++)
	{
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", entries[i].name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && entries[i].size
			&& fwrite (entries[i].data, 1, entries[i].size, F.f) != entries[i].size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing PCK2 member failed: %s\n", path);
		const enumError close_err = ResetFile (&F, opt_preserve);
		if (close_err > err)
			err = close_err;
	}
	FREE (entries);
	FREE (raw);
	if (!err && count && !testmode && (opt_recurse < 0 || depth < (uint)opt_recurse))
		err = extract_tree_complete (dest, depth + 1);
	return err;
}
