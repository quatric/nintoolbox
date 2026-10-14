// Tecmo resource packs (Rygar: The Battle of Argus, Wii; ".mad" maps, ".lad" actors, ".efe" effects, ".clf"),
// big-endian: an 8-byte pack tag ("MAD\0", "LAD\0", "EFE\0", "CLF\0", "yoyo", "player", ...), u32 member count @8,
// u32 file size @0x10, u32 0x20 @0x14 and @0x18, then count u32 member offsets at 0x20 (0 = unused slot; members end
// at the next larger offset (the table need not be sorted) or the end of the file; repeated offsets are written
// once). Every member starts with its own tag ("tgs\0", "XMDF",
// "texa", "ORB\0", ...); they are written as "NNNN_tag.bin" without interpreting them further.
static enumError extract_tecmoad_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".mad") && !is_ext (arg, ".lad") && !is_ext (arg, ".efe") && !is_ext (arg, ".clf"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t cnt = sz >= 0x24 ? rd_be32 (raw + 8) : 0;
	if (sz < 0x24 || rd_be32 (raw + 0x10) != sz || rd_be32 (raw + 0x14) != 0x20
		|| rd_be32 (raw + 0x18) != 0x20 || !cnt || cnt > 0x1000 || 0x20 + 4 * cnt > sz)
		goto skip;
	for (size_t i = 0; i < cnt; i++)
	{
		const size_t o = rd_be32 (raw + 0x20 + 4 * i);
		if (o && (o < 0x20 + 4 * cnt || o + 8 > sz))
			goto skip;
	}
	nintendo_sarc_entry_t *entries = CALLOC (cnt, sizeof (*entries));
	uint n = 0;
	bool good = entries != 0;
	for (size_t i = 0; i < cnt && good; i++)
	{
		const size_t o = rd_be32 (raw + 0x20 + 4 * i);
		size_t e = sz;
		bool first = o != 0;
		for (size_t j = 0; j < cnt && first; j++)
		{
			const size_t p = rd_be32 (raw + 0x20 + 4 * j);
			if (p == o && j < i)
				first = false;
			else if (p > o && p < e)
				e = p;
		}
		if (!first)
			continue;
		char tag[8] = "";
		for (uint k = 0; k < 4 && isalnum (raw[o + k]); k++)
			tag[k] = raw[o + k];
		char name[32];
		snprintf (name, sizeof (name), "%04u_%s.bin", (uint)i, *tag ? tag : "data");
		good = OwnedEntryAdd (entries, n, name, raw + o, e - o);
		if (good)
			n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good && n)
		err = write_owned_entries (arg, basedir, depth, "TECMO", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
skip:
	FREE (raw);
	return ERR_NOTHING_TO_DO;
}
