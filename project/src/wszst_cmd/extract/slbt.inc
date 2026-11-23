// "SLBT" (".atb", animation/sprite banks of the Battle of Giants engine, about 80 DS games), little-endian: {char "SLBT", u32 unpacked size, u8 flag, u8 count, u16 0,
// count * u32 chunk table, u8 0, u32 payload size, then one Nintendo LZ10 stream (type byte 0x10)}, so the header is 0x11 + 4 * count bytes. The stream must unpack to
// exactly the stored size and use the whole payload. The unpacked data (tile pixels and offset tables, layout not decoded) is written as "<file>.d/<name>.bin".
// Reuses packlz_lz10 from packlz.inc. The "DTBT" variant of the same engine is left alone.
static enumError extract_slbt_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0, *out = 0;
	size_t sz = 0, used, n;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		goto done;
	if (sz < 0x16 || memcmp (raw, "SLBT", 4) || !raw[9])
		goto done;
	const size_t hl = 0x11 + 4 * (size_t)raw[9];
	if (hl >= sz || rd_le32 (raw + hl - 4) != sz - hl)
		goto done;
	if (!packlz_lz10 (raw + hl, sz - hl, &used, &out, &n) || n != rd_le32 (raw + 4) || used != sz - hl)
		goto done;
	nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	const char *s = strrchr (arg, '/');
	char name[PATH_MAX];
	snprintf (name, sizeof (name), "%s.bin", s ? s + 1 : arg);
	if (OwnedEntryAdd (entries, 0, name, out, (uint)n))
	{
		err = write_owned_entries (arg, basedir, depth, "SLBT", entries, 1);
	}
	else
		ResetOwnedEntries (entries, 1);
done:
	FREE (out);
	FREE (raw);
	return err;
}
