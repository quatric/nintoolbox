// "OMOM" quest data archive (Monster Hunter 3 tri- / G, Wii: quest/quest*.bin), big-endian:
//   {"OMOM", u32 count, count * {u32 offset, u32 size}}, members at 0x40 aligned offsets (the member data starts at
//   the first offset, the table in front of it is zero padded). The members carry no names and are written as
//   "NNN.bin" into "<file>.d/" and then extracted recursively.
static enumError extract_omom_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 n = sz >= 8 ? rd_be32 (raw + 4) : 0;
	bool good = sz >= 8 && !memcmp (raw, "OMOM", 4) && n && n <= 4096 && 8 + (size_t)n * 8 <= sz;
	for (u32 i = 0; good && i < n; i++)
	{
		const size_t o = rd_be32 (raw + 8 + 8 * (size_t)i), s = rd_be32 (raw + 12 + 8 * (size_t)i);
		if (o < 8 + (size_t)n * 8 || o > sz || s > sz - o)
			good = false;
	}
	if (!good)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC ((size_t)n, sizeof (*entries));
	uint cnt = 0;
	for (u32 i = 0; entries && i < n; i++)
	{
		const size_t o = rd_be32 (raw + 8 + 8 * (size_t)i), s = rd_be32 (raw + 12 + 8 * (size_t)i);
		char name[32];
		snprintf (name, sizeof (name), "%03u.bin", i);
		if (OwnedEntryAdd (entries, cnt, name, raw + o, (uint)s))
			cnt++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (entries && cnt)
		err = write_owned_entries (arg, basedir, depth, "OMOM", entries, cnt);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
