// Pikmin 1 (USA) "SndData/Seqs/pikiseq.arc": the 17 JAudio ".jam" sequences back to back. The archive has no header of
// its own (the table lives in the game's pikiseq.hed, embedded in the DOL); the offsets below are the decompilation's
// table adjusted for this retail file: each sequence start was checked against the "opentrack" (0xC1) preamble every
// JAM begins with, and the last one ends exactly at EOF (220,992 bytes).
static enumError extract_pikmin_seq_file (ccp arg, ccp basedir, uint depth)
{
	static const struct { ccp name; u32 start; } tab[] = {
		{ "pikise.jam", 0x0 }, { "sysevent.jam", 0x2c40 }, { "tutorial.jam", 0x47a0 }, { "play3.jam", 0xab20 },
		{ "d_end2.jam", 0xed80 }, { "jungle.jam", 0x115a0 }, { "yaku.jam", 0x14c20 }, { "cave.jam", 0x18920 },
		{ "boss2.jam", 0x1dfc0 }, { "map.jam", 0x202c0 }, { "demobgm.jam", 0x232e0 }, { "boss3.jam", 0x2a6e0 },
		{ "flow.jam", 0x2cc80 }, { "select.jam", 0x312e0 }, { "char.jam", 0x31740 }, { "cresult.jam", 0x32d00 },
		{ "fresult.jam", 0x34ec0 } };
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	if (strcasecmp (base, "pikiseq.arc"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz != 220992 || raw[0] != 0xfe)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint n = sizeof (tab) / sizeof (*tab);
	nintendo_sarc_entry_t *entries = CALLOC (n, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint cnt = 0;
	for (uint i = 0; i < n; i++)
	{
		const u32 end = i + 1 < n ? tab[i + 1].start : (u32)sz;
		if (OwnedEntryAdd (entries, cnt, tab[i].name, raw + tab[i].start, end - tab[i].start))
			cnt++;
	}
	FREE (raw);
	if (!cnt)
	{
		FREE (entries);
		return ERR_NOTHING_TO_DO;
	}
	return write_owned_entries (arg, basedir, depth, "PIKMIN-SEQ", entries, cnt);
}
