// Genki "LXTools_FileCombine" self contained bundles (Spectrobes: Origins ".LXMF": character models, effects and sound banks; big endian, 1084 retail files). Layout: "FMXL", 01 01 01 00, u32 member
// count n at +8, further header words, then n entries of 0x20 bytes from +0x40 {0, u32 offset, 0, u32 padded size, 0, u32 offset of the NUL terminated path, 0x40 (path field size), u32 size}; the
// members (BRRES models and animations, "REFF" effects, PARAM.BIN, nested ".LXMF" sound banks) follow back to back and the 0x40 byte path fields sit after them. Members are written to "<file>.d/<path>"
// and nested bundles unpack further. The 94 ".LXMF" files with version words {0, 0, 1} (character motion keys) have a different layout and are not decoded.
static enumError extract_lxmf_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".lxmf"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x60 || sz > UINT_MAX || memcmp (raw, "FMXL\x01\x01\x01\x00", 8))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 n = rd_be32 (raw + 8);
	if (!n || n > 100000 || 0x40 + 0x20ull * n > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (n, sizeof (*entries));
	uint ne = 0;
	bool ok = entries != 0;
	for (u32 i = 0; ok && i < n; i++)
	{
		const u8 *r = raw + 0x40 + 0x20 * i;
		const u32 off = rd_be32 (r + 4), pad = rd_be32 (r + 12), no = rd_be32 (r + 20), size = rd_be32 (r + 28);
		ok = !rd_be32 (r) && !rd_be32 (r + 8) && !rd_be32 (r + 16) && rd_be32 (r + 24) == 0x40 && pad >= size && (u64)off + size <= sz && no < sz && memchr (raw + no, 0, sz - no);
		if (ok)
			ok = OwnedEntryAdd (entries, ne, (ccp)raw + no, raw + off, size);
		if (ok)
			ne++;
	}
	FREE (raw);
	if (!ok || !ne)
	{
		if (entries)
			ResetOwnedEntries (entries, ne);
		return ERR_NOTHING_TO_DO;
	}
	return write_owned_entries (arg, basedir, depth, "LXMF", entries, ne);
}
