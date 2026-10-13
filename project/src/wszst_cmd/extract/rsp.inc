// PlatinumGames "PACK" sound packs (MadWorld, Wii; ".rsp"): u32 "PACK", u32 count, then count
// {u32 offset, u32 size} pairs (offsets from the start of the pack, which may itself be a member of an
// outer pack). A member starting with "PACK" is a nested pack, every other member is a RedSpark ".rsd"
// sound. Members are written as "<NN>.rsd" ("<NN>-<MM>.rsd" when nested) for the RedSpark decoder.
static bool rsp_collect (const u8 *raw, size_t sz, const char *prefix, nintendo_sarc_entry_t *entries, uint *n, uint level)
{
	const uint count = sz >= 8 ? rd_be32 (raw + 4) : 0;
	if (level > 3 || memcmp (raw, "PACK", 4) || !count || count > 256 || 8 + 8 * (size_t)count > sz)
		return false;
	for (uint i = 0; i < count; i++)
	{
		const size_t off = rd_be32 (raw + 8 + 8 * i), size = rd_be32 (raw + 12 + 8 * i);
		if (!size)
			continue;
		if (off < 8 + 8 * (size_t)count || off > sz || size > sz - off || *n >= 1024)
			return false;
		char name[48];
		snprintf (name, sizeof (name), "%s%02u", prefix, i);
		if (size >= 8 && !memcmp (raw + off, "PACK", 4))
		{
			char sub[48];
			snprintf (sub, sizeof (sub), "%s-", name);
			if (!rsp_collect (raw + off, size, sub, entries, n, level + 1))
				return false;
			continue;
		}
		strcat (name, ".rsd");
		if (!OwnedEntryAdd (entries, *n, name, (u8 *)raw + off, size))
			return false;
		(*n)++;
	}
	return true;
}

static enumError extract_rsp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".rsp"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	nintendo_sarc_entry_t *entries = CALLOC (1024, sizeof (*entries));
	uint n = 0;
	if (!entries)
		err = ERR_OUT_OF_MEMORY;
	else if (rsp_collect (raw, sz, "", entries, &n, 0) && n)
		err = write_owned_entries (arg, basedir, depth, "RSP", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
