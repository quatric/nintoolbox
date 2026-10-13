// Ganbarion "ALAR" archives, big-endian flavour (Pandora's Tower, Wii; ".aar"): "ALAR", u8 2, u8 0xe1, u16
// member count, then count 16-byte records at 0x10 {u8 kind ('A'..'D'), u8 id[3], u32 offset, u32 size,
// u32 flags, bit 31 set}. Members are tagged data blocks ("ALIG", "ALOD", "ALMT", "ALMS", ...) and are written as
// "NNN.<tag>" (kind + id in the name when the tag is unknown). Not the Jump Ultimate Stars ALAR (alar.inc).
static enumError extract_alar2_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".aar"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 0x20 ? rd_be16 (raw + 6) : 0;
	bool ok = !memcmp (raw, "ALAR", 4) && raw[4] == 2 && count && 0x10 + 16 * (size_t)count <= sz;
	for (uint i = 0; ok && i < count; i++)
	{
		const u8 *r = raw + 0x10 + 16 * i;
		const size_t o = rd_be32 (r + 4), n = rd_be32 (r + 8);
		ok = (rd_be32 (r + 12) & 0x80000000) && o >= 0x10 + 16 * (size_t)count && o <= sz && n <= sz - o;
	}
	if (!ok)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (count, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	for (uint i = 0; i < count && ok; i++)
	{
		const u8 *r = raw + 0x10 + 16 * i;
		const size_t o = rd_be32 (r + 4), len = rd_be32 (r + 8);
		char tag[8] = "bin";
		if (len >= 4 && !memcmp (raw + o, "AL", 2) && isupper (raw[o + 2]) && isupper (raw[o + 3]))
		{
			memcpy (tag, raw + o, 4);
			tag[4] = 0;
			for (char *q = tag; *q; q++)
				*q = (char)tolower ((u8)*q);
		}
		char name[48];
		snprintf (name, sizeof (name), "%03u_%c%02x%02x%02x.%s", i, isprint (r[0]) ? r[0] : '_', r[1], r[2], r[3], tag);
		ok = OwnedEntryAdd (entries, n, name, raw + o, (uint)len);
		n += ok;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && n)
		err = write_owned_entries (arg, basedir, depth, "ALAR2", entries, n);
	else
		ResetOwnedEntries (entries, n);
	FREE (raw);
	return err;
}
