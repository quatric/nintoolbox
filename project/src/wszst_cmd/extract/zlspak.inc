// Hash indexed "PAK\0" packages (Kid Adventures: Sky Captain "packfile/packfile.dat"), big-endian: "PAK\0", u32 count,
// then count records {u32 name hash, u32 absolute offset, u32 size} sorted by hash (the hash algorithm is unknown, so
// members are named "<hash>.bin"). Members are either stored or "!ZLS", u32 little-endian unpacked size, zlib stream.
static enumError extract_zlspak_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dat") && !is_ext (arg, ".pak"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 20 || memcmp (raw, "PAK\0", 4))
		goto done;
	const uint count = rd_be32 (raw + 4);
	if (!count || count > 100000 || 8 + 12 * (size_t)count > sz)
		goto done;
	u32 prev = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + 8 + 12 * (size_t)i;
		const size_t o = rd_be32 (e + 4), s = rd_be32 (e + 8);
		if (rd_be32 (e) < prev || o < 8 + 12 * (size_t)count || o > sz || s > sz - o)
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
		const u8 *e = raw + 8 + 12 * (size_t)i;
		const size_t o = rd_be32 (e + 4), s = rd_be32 (e + 8);
		char name[32];
		snprintf (name, sizeof (name), "%08x.bin", rd_be32 (e));
		bool good;
		if (s >= 8 && !memcmp (raw + o, "!ZLS", 4))
		{
			uLongf dl = rd_le32 (raw + o + 4);
			u8 *body = dl < (512u << 20) ? MALLOC (dl ? dl : 1) : 0;
			if (body && uncompress (body, &dl, raw + o + 8, (uLong)(s - 8)) == Z_OK && dl == rd_le32 (raw + o + 4))
				good = OwnedEntryAdd (entries, k, name, body, (uint)dl);
			else
				good = OwnedEntryAdd (entries, k, name, raw + o, (uint)s);
			FREE (body);
		}
		else
			good = OwnedEntryAdd (entries, k, name, raw + o, (uint)s);
		if (good)
			k++;
	}
	if (k)
		err = write_owned_entries (arg, basedir, depth, "ZLSPAK", entries, k);
	else
		ResetOwnedEntries (entries, count);
done:
	FREE (raw);
	return err;
}
