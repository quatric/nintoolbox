// Marvelous "pBin" containers (Harvest Moon: Animal Parade, Wii; ".pBin .cmp .tak .motma ..."). Big-endian:
// "pBin", u32 1, u32 1, u32 0xffffffff, u32 table offset @0x10 (0x20), u32 entry count, u32 size of the main
// body @0x18 and u32 offset of the main body @0x1c. At the table offset sit the 4-character type of the main
// body (DAT, FMS, ...) and then count 16-byte entries {u32 0, u32 size, u32 offset, char tag[4]}, the tag
// naming the member (FMS model, TEX texture set, MOT motion, ...). The main body is written as "main.<tag>",
// the members as "NN.<tag>" (tags in lower case; TEX becomes ".ftex" so it is not taken for a Nintendo TEX0).
static enumError extract_pbin_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (export_count == 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x20 || memcmp (raw, "pBin", 4) || rd_be32 (raw + 4) != 1 || rd_be32 (raw + 12) != 0xffffffff)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t tab = rd_be32 (raw + 0x10);
	const uint count = rd_be32 (raw + 0x14), msz = rd_be32 (raw + 0x18), moff = rd_be32 (raw + 0x1c);
	if (!count || count > 4096 || tab > sz || sz - tab < 4 + 16 * (size_t)count)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC ((size_t)count + 1, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	bool ok = true;
	if (msz && moff <= sz && msz <= sz - moff)
	{
		char mt[5], mn[16];
		memcpy (mt, raw + tab, 4);
		mt[4] = 0;
		for (char *p = mt; *p; p++)
			*p = isalnum ((u8)*p) ? (char)tolower ((u8)*p) : '_';
		for (size_t l = strlen (mt); l && mt[l - 1] == '_'; )
			mt[--l] = 0;
		snprintf (mn, sizeof (mn), "main.%s", !*mt ? "bin" : !strcmp (mt, "tex") ? "ftex" : mt);
		ok = OwnedEntryAdd (entries, n, mn, raw + moff, msz);
		n += ok;
	}
	for (uint i = 0; i < count && ok; i++)
	{
		const u8 *e = raw + tab + 4 + 16 * i;
		const size_t size = rd_be32 (e + 4), off = rd_be32 (e + 8);
		if (!size)
			continue;
		if (off > sz || size > sz - off)
		{
			ok = false;
			break;
		}
		char tag[5], name[32];
		memcpy (tag, e + 12, 4);
		tag[4] = 0;
		for (char *p = tag; *p; p++)
			*p = isalnum ((u8)*p) ? (char)tolower ((u8)*p) : '_';
		for (size_t l = strlen (tag); l && tag[l - 1] == '_'; )
			tag[--l] = 0;
		snprintf (name, sizeof (name), "%02u.%s", i, !*tag ? "bin" : !strcmp (tag, "tex") ? "ftex" : tag);
		ok = OwnedEntryAdd (entries, n, name, raw + off, (uint)size);
		n += ok;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && n)
		err = write_owned_entries (arg, basedir, depth, "PBIN", entries, n);
	else
		ResetOwnedEntries (entries, n);
	FREE (raw);
	return err;
}
