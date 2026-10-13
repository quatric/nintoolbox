// Atlus "TPX" texture packs (Trauma Team, Wii; ".tpx"), big-endian: u32 build date (0x2006xxxx), u32 small count-like field, u32 header
// size (0x20 + 0x20 * count), u32 count, then count 0x20-byte records at 0x20 {u32 flags, u32 offset, u32 size,
// char name[0x14]} followed by per-image info blocks. Every record points at a standard TPL; the TPLs are written
// as "<name>.tpl" (the name stops at the first space or non-ASCII byte) and decode to PNG like any other TPL.
static enumError extract_tpx_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".tpx"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 0x40 ? rd_be32 (raw + 12) : 0;
	bool ok = count && count <= 64 && rd_be32 (raw + 4) - 1 < 64 && rd_be32 (raw + 8) == 0x20 + 0x20 * count
		&& (rd_be32 (raw) >> 24) == 0x20 && 0x20 + 0x20 * (size_t)count <= sz;
	for (uint i = 0; ok && i < count; i++)
	{
		const u8 *r = raw + 0x20 + 0x20 * (size_t)i;
		const size_t o = rd_be32 (r + 4), n = rd_be32 (r + 8);
		ok = o >= 0x20 + 0x20 * (size_t)count && o <= sz && n >= 0x14 && n <= sz - o && rd_be32 (raw + o) == 0x0020af30;
	}
	nintendo_sarc_entry_t *entries = ok ? CALLOC (count, sizeof (*entries)) : 0;
	if (!entries)
	{
		FREE (raw);
		return ok ? ERR_OUT_OF_MEMORY : ERR_NOTHING_TO_DO;
	}
	uint n = 0;
	for (uint i = 0; i < count && ok; i++)
	{
		const u8 *r = raw + 0x20 + 0x20 * (size_t)i;
		char name[0x20];
		uint l = 0;
		while (l < 0x14 && r[12 + l] > 0x20 && r[12 + l] < 0x7f && r[12 + l] != '/' && r[12 + l] != '\\')
			l++;
		if (l)
			snprintf (name, sizeof (name), "%.*s.tpl", (int)l, (ccp)r + 12);
		else
			snprintf (name, sizeof (name), "%02u.tpl", i);
		for (uint j = 0; j < n; j++)
			if (!strcmp (entries[j].name, name))
				snprintf (name, sizeof (name), "%02u_%.*s.tpl", i, (int)l, (ccp)r + 12);
		ok = OwnedEntryAdd (entries, n, name, raw + rd_be32 (r + 4), rd_be32 (r + 8));
		n += ok;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && n)
		err = write_owned_entries (arg, basedir, depth, "TPX", entries, n);
	else
		ResetOwnedEntries (entries, n);
	FREE (raw);
	return err;
}
