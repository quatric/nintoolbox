// Call of Duty: Black Ops / Modern Warfare 3 (Wii) sound.pak. The PAK0
// table has no filenames: each member is identified by a CRC and stored at
// DATASTART + OFFSET * MULTIPLIER. The payloads are raw Nintendo DSP audio.
static enumError extract_cod_pak0_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pak"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX || raw_size < 20 || memcmp (raw, "PAK0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const uint n_entries = le32 (raw + 8);
	const uint multiplier = le32 (raw + 12);
	const uint data_start = le32 (raw + 16);
	if (!n_entries || !multiplier || n_entries > (raw_size - 20) / 12)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t table_end = 20 + (size_t)n_entries * 12;
	if (data_start < table_end || data_start > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Validate every member before creating any output. Apart from avoiding
	// partial extraction of corrupt archives, 64-bit arithmetic prevents a
	// crafted OFFSET * MULTIPLIER from wrapping back into the table.
	for (uint i = 0; i < n_entries; i++)
	{
		const u8 *entry = raw + 20 + i * 12;
		const uint64_t off = (uint64_t)le32 (entry + 4) * multiplier + data_start;
		const uint size = le32 (entry + 8);
		if (off > raw_size || size > raw_size - off)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT COD PAK0:%s (%u DSP entries) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n_entries, dest);

	char stem[PATH_MAX];
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	snprintf (stem, sizeof (stem), "%s", base);
	char *dot = strrchr (stem, '.');
	if (dot)
		*dot = 0;
	if (!*stem || !OwnedNameOk (stem))
		snprintf (stem, sizeof (stem), "sound");

	for (uint i = 0; !err && i < n_entries; i++)
	{
		const u8 *entry = raw + 20 + i * 12;
		const uint crc = le32 (entry);
		const uint64_t off = (uint64_t)le32 (entry + 4) * multiplier + data_start;
		const uint size = le32 (entry + 8);
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (
			path, sizeof (path), "%s/%s%s_0x%08x.dsp", dest, basedir ? basedir : "", stem, crc);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && size && fwrite (raw + off, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
		ResetFile (&F, opt_preserve);
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
