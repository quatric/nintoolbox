// AT7 container archive extraction (Pokémon Mystery Dungeon WiiWare data*.bin / data*.raw /
// data*.at7). Either uncompressed raw data or compressed (AT7P/AT7X/AT7E) stream. Table starts at
// offset 0 with 28-byte (0x1C) records: uint32_be file_offset, uint32_be file_size, char
// file_name[20]. Ends when file_offset == 0 or record matches namesEnd.
static enumError extract_at7_file (ccp arg, ccp basedir, uint depth)
{
	// Unlike every sibling extract_*_file(), AT7 has no fixed extension, so
	// this runs against every file reaching extract_one_file() -- including
	// multi-gigabyte pass-through sources (e.g. a Switch Program NCA). Cap
	// the load with LoadFileAlloc's own max_size so those are rejected
	// cheaply instead of fully read into memory first. The raw EFBIG errno
	// (27) must never be returned here: it aliases ERU_WARNING in this
	// file's own enumError enum, which silently aborts the whole XX
	// pipeline with no message -- confirmed live on Super Mario Odyssey's
	// 5.5 GB Program NCA, which never reached the hactool pass-through
	// because of this.
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, UINT_MAX, 2, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 28)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *decomp = 0;
	uint decomp_size = 0;
	if (!memcmp (raw, "AT7P", 4) || !memcmp (raw, "AT7X", 4))
	{
		err = DecodeAT7 (&decomp, &decomp_size, raw, (uint)raw_size);
		FREE (raw);
		if (err)
			return ERR_NOTHING_TO_DO;
		raw = decomp;
		raw_size = decomp_size;
		if (raw_size < 28)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}

	// Check if raw is an AT7 table of contents
	uint n_entries = 0;
	uint cur = 0;
	while (cur + 28 <= raw_size)
	{
		uint off = be32 (raw + cur);
		uint sz = be32 (raw + cur + 4);
		if (!off)
			break; // End of table
		if (off < 28 || (u64)off + sz > raw_size)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		// Check filename characters
		bool valid_name = false;
		for (uint k = 8; k < 28; k++)
		{
			u8 ch = raw[cur + k];
			if (!ch)
			{
				if (k > 8)
					valid_name = true;
				break;
			}
			if (ch < 0x20 || ch > 0x7E)
				break;
		}
		if (!valid_name)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		n_entries++;
		cur += 28;
	}

	if (!n_entries)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT AT7:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);

	cur = 0;
	for (uint i = 0; !err && i < n_entries; i++, cur += 28)
	{
		uint off = be32 (raw + cur);
		uint sz = be32 (raw + cur + 4);
		char name[24] = { 0 };
		memcpy (name, raw + cur + 8, 20);
		name[20] = 0;
		if (!valid_at7_path (name))
		{
			err = ERROR0 (ERR_INVALID_DATA, "Unsafe AT7 entry path: %s\n", name);
			break;
		}
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && sz && fwrite (raw + off, 1, sz, F.f) != sz)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", sz, path);
		const enumError close_err = ResetFile (&F, opt_preserve);
		if (close_err > err)
			err = close_err;
	}

	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
