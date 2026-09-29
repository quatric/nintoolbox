// Namco/Tose's ARCV archive, used by Pac-Man Party (Wii).  Despite the
// familiar .arc extension this is neither Nintendo U8 nor Brawl PAC.  The
// little-endian header is "ARCV", member count and total archive size,
// followed by count { offset, size, crc32 } records.  There are deliberately
// no member names in this variant, so retain the on-disk ordinal and infer a
// useful extension only when the embedded file has a known magic.
static ccp arcv_member_ext (const u8 *data, uint size)
{
	if (size < 4)
		return ".bin";
	if (!memcmp (data, "bres", 4))
		return ".brres";
	if (!memcmp (data, "RSEQ", 4))
		return ".rseq";
	if (!memcmp (data, "RSTM", 4))
		return ".rstm";
	if (!memcmp (data, "RWAV", 4))
		return ".rwav";
	if (!memcmp (data, "RARC", 4))
		return ".rarc";
	if (!memcmp (data, "ARC\\0", 4))
		return ".pac";
	return ".bin";
}

static enumError extract_arcv_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".arc"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 12 || raw_size > UINT_MAX || memcmp (raw, "ARCV", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const uint n_entries = le32 (raw + 4);
	const uint archive_size = le32 (raw + 8);
	if (!n_entries || n_entries > (raw_size - 12) / 12 || archive_size != raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const uint table_end = 12 + n_entries * 12;
	for (uint i = 0; i < n_entries; i++)
	{
		const u8 *entry = raw + 12 + i * 12;
		const uint off = le32 (entry);
		const uint size = le32 (entry + 4);
		if (off < table_end || off > raw_size || size > raw_size - off)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ARCV:%s (%u unnamed entries) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n_entries, dest);

	for (uint i = 0; !err && i < n_entries; i++)
	{
		const u8 *entry = raw + 12 + i * 12;
		const uint off = le32 (entry);
		const uint size = le32 (entry + 4);
		const u8 *data = raw + off;
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%sfile_%04u%s", dest, basedir ? basedir : "", i,
			arcv_member_ext (data, size));
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && size && fwrite (data, 1, size, F.f) != size)
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
