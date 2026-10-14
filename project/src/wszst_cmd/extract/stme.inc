// "STME" streaming packages (Need for Speed: Hot Pursuit, Wii; ".stm" environment files), big-endian: "STME", bytes
// 01 01 01 0f, platform "WI01", u32 record count @0xc, then count 0x40-byte records at 0x10 {u32 kind, char
// name[0x24] (NUL-terminated, e.g. "advt_rio1_adverta.mp1" / ".tex"), ..., u32 data offset @0x28, u32 size @0x2c,
// u32 header size @0x30, ...}. Textures appear twice (".mp1" with a 0x60-byte header and ".tex") pointing at the
// same bytes. Every record with data is written as "NNNN_name" (the payloads, GX textures and model data, are not
// interpreted).
static enumError extract_stme_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".stm"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t cnt = sz >= 0x10 ? rd_be32 (raw + 0xc) : 0;
	if (sz < 0x50 || memcmp (raw, "STME", 4) || memcmp (raw + 8, "WI01", 4) || !cnt || cnt > 0x20000
		|| 0x10 + 0x40 * cnt > sz)
		goto skip;
	for (size_t i = 0; i < cnt; i++)
	{
		const u8 *r = raw + 0x10 + 0x40 * i;
		const size_t off = rd_be32 (r + 0x28), n = rd_be32 (r + 0x2c);
		if (off > sz || n > sz - off || rd_be32 (r + 0x30) > n)
			goto skip;
	}
	nintendo_sarc_entry_t *entries = CALLOC (cnt, sizeof (*entries));
	uint n = 0;
	bool good = entries != 0;
	for (size_t i = 0; i < cnt && good; i++)
	{
		const u8 *r = raw + 0x10 + 0x40 * i;
		const size_t off = rd_be32 (r + 0x28), len = rd_be32 (r + 0x2c);
		if (!len)
			continue;
		char nm[0x25] = "";
		for (size_t k = 0; k < 0x24 && r[4 + k]; k++)
			nm[k] = isalnum (r[4 + k]) || r[4 + k] == '_' || r[4 + k] == '-' || r[4 + k] == '.' ? r[4 + k] : '_';
		char name[64];
		snprintf (name, sizeof (name), "%04u_%s", (uint)i, *nm ? nm : "data");
		good = OwnedEntryAdd (entries, n, name, raw + off, len);
		if (good)
			n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good && n)
		err = write_owned_entries (arg, basedir, depth, "STME", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
skip:
	FREE (raw);
	return ERR_NOTHING_TO_DO;
}
