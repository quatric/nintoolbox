// "GLOB" resource packs (Green Lantern: Rise of the Manhunters, Wii; ".glob" data + ".name.glob" name table, big endian, 422 packs): both files start {"GLOB", u32 entry count, ...}. From 0x08 the name
// file lists {u32 id, u32 offset of the name, u32 length (with the terminating NUL)} per resource plus an end sentinel with id 0x7fffffff ("Textures/Maps/Coastcity/c_sidewalk.gaftx"), the data file {u32 id, u32 offset, u32 size} (ids match,
// the data is stored back to back after the table). Opening the data file writes every resource under its name to "<file>.d/", extracted recursively.
static enumError extract_glob_pack_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".glob") || is_ext (arg, ".name.glob"))
		return ERR_NOTHING_TO_DO;
	char npath[PATH_MAX];
	snprintf (npath, sizeof (npath), "%s", arg);
	char *dot = strrchr (npath, '.');
	if (!dot)
		return ERR_NOTHING_TO_DO;
	strcpy (dot, ".name.glob");
	u8 *raw = 0, *nm = 0;
	size_t sz = 0, nsz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x18 || memcmp (raw, "GLOB", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (LoadFileAlloc (npath, 0, 0, &nm, &nsz, 0, 0, 0, false) || !nm || nsz < 0x18 || memcmp (nm, "GLOB", 4))
	{
		FREE (raw);
		FREE (nm);
		return ERR_NOTHING_TO_DO;
	}
	u32 n = rd_be32 (raw + 4);
	bool ok = n > 1 && n < 0x100000 && rd_be32 (nm + 4) == n && 8 + 12ull * n <= sz && 8 + 12ull * n <= nsz && sz < 0xffffffffu;
	if (ok && rd_be32 (raw + 8 + 12 * (size_t)(n - 1)) == 0x7fffffff)
		n--; // end sentinel {0x7fffffff, file size, 0}
	for (u32 i = 0; ok && i < n; i++)
	{
		const u8 *d = raw + 8 + 12 * (size_t)i, *e = nm + 8 + 12 * (size_t)i;
		const u64 off = rd_be32 (d + 4), size = rd_be32 (d + 8), no = rd_be32 (e + 4), nl = rd_be32 (e + 8);
		ok = rd_be32 (d) == rd_be32 (e) && off + size <= sz && no + nl <= nsz && nl > 1 && nl < 512 && !nm[no + nl - 1];
	}
	nintendo_sarc_entry_t *entries = ok ? CALLOC (n, sizeof (*entries)) : 0;
	bool good = ok && entries;
	uint cnt = 0;
	for (u32 i = 0; good && i < n; i++)
	{
		const u8 *d = raw + 8 + 12 * (size_t)i, *e = nm + 8 + 12 * (size_t)i;
		const u32 off = rd_be32 (d + 4), size = rd_be32 (d + 8);
		char name[600];
		snprintf (name, sizeof (name), "%s", (ccp)nm + rd_be32 (e + 4));
		good = OwnedEntryAdd (entries, cnt, name, raw + off, size);
		if (good)
			cnt++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good && cnt)
		err = write_owned_entries (arg, basedir, depth, "GLOB", entries, cnt);
	else
		FREE (entries);
	FREE (raw);
	FREE (nm);
	return err;
}
