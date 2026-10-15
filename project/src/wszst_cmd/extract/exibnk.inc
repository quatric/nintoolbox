// Exient sound bank ".bnk" (Need for Speed: Hot Pursuit, Wii), big-endian: u32 count, then count 12-byte entries
// {u32 offset, u32 size, u32 0}, ending in 0xffffffff words; every member is a complete "WIIADPCM" stream, written as
// "NNNN.adpcm" (which decodes through the WIIADPCM extractor to per-channel DSP / WAV).
static enumError extract_exibnk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bnk"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t cnt = sz >= 4 ? rd_be32 (raw) : 0;
	if (!cnt || cnt > 0x4000 || 4 + 12 * cnt > sz)
		goto skip;
	for (size_t i = 0; i < cnt; i++)
	{
		const size_t off = rd_be32 (raw + 4 + 12 * i), n = rd_be32 (raw + 8 + 12 * i);
		if (off < 4 + 12 * cnt || off > sz || n < 0x100 || n > sz - off || rd_be32 (raw + 12 + 12 * i)
			|| memcmp (raw + off, "WIIADPCM", 8))
			goto skip;
	}
	nintendo_sarc_entry_t *entries = CALLOC (cnt, sizeof (*entries));
	bool good = entries != 0;
	for (size_t i = 0; i < cnt && good; i++)
	{
		char name[24];
		snprintf (name, sizeof (name), "%04u.adpcm", (uint)i);
		good = OwnedEntryAdd (entries, i, name, raw + rd_be32 (raw + 4 + 12 * i), rd_be32 (raw + 8 + 12 * i));
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good)
		err = write_owned_entries (arg, basedir, depth, "EXIBNK", entries, cnt);
	else
		FREE (entries);
	FREE (raw);
	return err;
skip:
	FREE (raw);
	return ERR_NOTHING_TO_DO;
}
