// "HOG" archives (Ultimate Band, Disney Hannah Montana; all little-endian):
//   u16 version = 1, u16 = 2, u32 header_size = 0x20, u32 0, u32 hash,
//   u32 n_entries, u32 names_size, u32 pad;
//   n_entries * {u32 name_offset (absolute), u32 data_offset (absolute),
//                u32 size, u32 name_hash}; NUL-terminated names with '\' paths.
// Members are stored uncompressed.
static enumError extract_hog_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".hog"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 0x30 || raw_size > UINT_MAX || raw[0] != 1 || raw[1] || raw[2] != 2 || raw[3]
		|| rd_le32 (raw + 4) != 0x20)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = rd_le32 (raw + 0x10);
	if (!count || count > 100000 || 0x20 + 16ull * count > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = CALLOC (count, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + 0x20 + 16 * (size_t)i;
		const u32 no = rd_le32 (e), off = rd_le32 (e + 4), sz = rd_le32 (e + 8);
		if (no >= raw_size || (u64)off + sz > raw_size || !memchr (raw + no, 0, raw_size - no))
			continue;
		char name[260];
		snprintf (name, sizeof (name), "%s", (ccp)raw + no);
		for (char *c = name; *c; c++)
			if (*c == '\\')
				*c = '/';
		if (!OwnedNameOk (name))
			snprintf (name, sizeof (name), "%04u.bin", i);
		if (!OwnedEntryAdd (entries, n, name, raw + off, sz))
			break;
		n++;
	}
	if (!n)
	{
		FREE (entries);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	err = write_owned_entries (arg, basedir, depth, "HOG", entries, n);
	FREE (raw);
	return err;
}
