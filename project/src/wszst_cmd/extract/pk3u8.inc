// Simple Series ".pk3" packs (Simple Series Vol. 3: The Mahjong, WiiWare): an 8-byte header (two words, not interpreted) followed by one plain payload: a U8 archive ("U\xaa8-", written as .arc), a BRRES ("bres", .brres),
// a TPL (.tpl) or anything else (.bin). The payload is written as "<file>.d/<file>.<ext>" and then unpacked like any other file.
static enumError extract_pk3u8_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pk3"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 8 + 0x20 && sz <= UINT_MAX)
	{
		ccp ext = !memcmp (raw + 8, "\x55\xaa\x38\x2d", 4) ? "arc" : !memcmp (raw + 8, "bres", 4) ? "brres" : rd_be32 (raw + 8) == 0x0020af30 ? "tpl" : "bin";
		nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
		const char *b = strrchr (arg, '/');
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s.%s", b ? b + 1 : arg, ext);
		if (!entries)
			err = ERR_OUT_OF_MEMORY;
		else if (OwnedEntryAdd (entries, 0, name, raw + 8, (uint)(sz - 8)))
			err = write_owned_entries (arg, basedir, depth, "PK3", entries, 1);
		else
			ResetOwnedEntries (entries, 1);
	}
	FREE (raw);
	return err;
}
