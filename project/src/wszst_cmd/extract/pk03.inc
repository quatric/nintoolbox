// Hash indexed "PK03" packages (Deer Drive Legends "Game.pak" / "GameStatic.pak"), big-endian: "PK03", u32 0, u32 count,
// then count records {u32 name hash, u32 offset, u32 size << 8 | flags} sorted by hash (algorithm unknown, members are
// named "<hash>.bin"); flag 1 = the member is an LZ11 stream, 0 = stored.
static enumError extract_pk03_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pak"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 24 || memcmp (raw, "PK03", 4))
		goto done;
	const uint count = rd_be32 (raw + 8);
	if (!count || count > 200000 || 12 + 12 * (size_t)count > sz)
		goto done;
	u32 prev = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + 12 + 12 * (size_t)i;
		const size_t o = rd_be32 (e + 4), s = rd_be32 (e + 8) >> 8;
		if (rd_be32 (e) < prev || (rd_be32 (e + 8) & 0xfe) || o < 12 + 12 * (size_t)count || o > sz || s > sz - o)
			goto done;
		prev = rd_be32 (e);
	}
	nintendo_sarc_entry_t *entries = CALLOC (count, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	uint k = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + 12 + 12 * (size_t)i;
		const size_t o = rd_be32 (e + 4), s = rd_be32 (e + 8) >> 8;
		char name[32];
		snprintf (name, sizeof (name), "%08x.bin", rd_be32 (e));
		for (uint j = 0; j < k; j++)
			if (!strcmp (entries[j].name, name))
			{
				snprintf (name, sizeof (name), "%08x_%u.bin", rd_be32 (e), i);
				break;
			}
		bool good = false;
		if (rd_be32 (e + 8) & 1)
		{
			u8 *out = 0;
			uint on = 0;
			if (s >= 4 && raw[o] == 0x11 && !DecodeLZ10LZ11 (&out, &on, raw + o, (uint)s) && out)
				good = OwnedEntryAdd (entries, k, name, out, on);
			else
				good = OwnedEntryAdd (entries, k, name, raw + o, (uint)s);
			FREE (out);
		}
		else
			good = OwnedEntryAdd (entries, k, name, raw + o, (uint)s);
		if (good)
			k++;
	}
	if (k)
		err = write_owned_entries (arg, basedir, depth, "PK03", entries, k);
	else
		ResetOwnedEntries (entries, count);
done:
	FREE (raw);
	return err;
}
