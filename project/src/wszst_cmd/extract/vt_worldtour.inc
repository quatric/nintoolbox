// Virtua Tennis 2009 "WorldTour.jam" (Sumo Digital game data, not a JAudio sequence): BE {u32 0x3be0b3b1, u32 count,
// count * u32 ids}, then `count` sections that each begin with the word 0x39d9c7d5 (followed by a section size-like word
// and zeros). Each section is written as "NN.bin" and every ASCII run of 5+ characters (player and venue names)
// as "strings.tsv" ("section<TAB>offset in section<TAB>text"). The section fields themselves are not interpreted.
static enumError extract_vt_worldtour_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".jam"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 16 || rd_be32 (raw) != 0x3be0b3b1u)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = rd_be32 (raw + 4);
	size_t *off = count && count <= 256 ? CALLOC (count + 1, sizeof (*off)) : 0;
	uint found = 0;
	for (size_t o = 8 + 4 * (size_t)count; off && o + 4 <= sz && found <= count; o += 4)
		if (rd_be32 (raw + o) == 0x39d9c7d5u)
			off[found++] = o;
	if (!off || found != count)
	{
		FREE (off);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	off[count] = sz;
	nintendo_sarc_entry_t *entries = CALLOC (count + 1, sizeof (*entries));
	char *txt = CALLOC (1, 1 + 64 * 1024);
	size_t tl = 0;
	bool good = entries && txt;
	for (uint i = 0; i < count && good; i++)
	{
		char name[24];
		snprintf (name, sizeof (name), "%02u.bin", i);
		good = OwnedEntryAdd (entries, i, name, raw + off[i], off[i + 1] - off[i]);
		for (size_t o = off[i]; good && o < off[i + 1];)
		{
			size_t e = o;
			while (e < off[i + 1] && raw[e] >= 0x20 && raw[e] < 0x7f)
				e++;
			uint letters = 0;
			for (size_t k = o; k < e; k++)
				letters += (raw[k] | 0x20) >= 'a' && (raw[k] | 0x20) <= 'z';
			if (e - o >= 5 && letters >= 4 && tl + (e - o) + 32 < 64 * 1024)
				tl += (size_t)snprintf (txt + tl, 64 * 1024 - tl, "%u\t%u\t%.*s\n", i, (uint)(o - off[i]), (int)(e - o), (ccp)raw + o);
			o = e > o ? e : o + 1;
		}
	}
	good = good && OwnedEntryAdd (entries, count, "strings.tsv", (u8 *)txt, tl);
	enumError err = ERR_NOTHING_TO_DO;
	if (good)
		err = write_owned_entries (arg, basedir, depth, "VT-WORLDTOUR", entries, count + 1);
	else
		FREE (entries);
	FREE (txt);
	FREE (off);
	FREE (raw);
	return err;
}
