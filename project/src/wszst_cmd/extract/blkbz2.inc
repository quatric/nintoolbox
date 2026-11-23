// The Amazing Brain Train "<name>.blk.bz2" block files (WiiWare), little-endian: {u32 1, u32 unpacked size, u32 unpacked size, u32 stored size} followed by one plain bzip2 stream ("BZh"). Written as "<file>.d/<name>.blk" (the block
// contents are not decoded).
static enumError extract_blkbz2_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bz2"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const u64 us = sz > 20 ? rd_le32 (raw + 4) : 0, cs = sz > 20 ? rd_le32 (raw + 12) : 0;
	if (sz > 20 && rd_le32 (raw) == 1 && us == rd_le32 (raw + 8) && us >= 4 && us <= 0x40000000 && cs == sz - 16 && !memcmp (raw + 16, "BZh", 3))
	{
		u8 *out = MALLOC (us);
		uint got = 0;
		nintendo_sarc_entry_t *entries = out ? CALLOC (1, sizeof (*entries)) : 0;
		if (!entries)
			err = ERR_OUT_OF_MEMORY;
		else if (!DecodeBZIP2buf (out, (uint)us, &got, raw + 16, (uint)cs) && got == us)
		{
			const char *b = strrchr (arg, '/');
			char name[PATH_MAX];
			snprintf (name, sizeof (name), "%s", b ? b + 1 : arg);
			char *e = strrchr (name, '.'); // drop ".bz2"
			if (e)
				*e = 0;
			if (OwnedEntryAdd (entries, 0, name, out, (uint)us))
			{
				err = write_owned_entries (arg, basedir, depth, "BLKBZ2", entries, 1);
				entries = 0;
			}
		}
		if (entries)
			ResetOwnedEntries (entries, 1);
		FREE (out);
	}
	FREE (raw);
	return err;
}
