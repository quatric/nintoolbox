// "add\0" packs (the content of Moki Moki ".pacz" files after LZS decompression), big-endian: {char "add\0", u32 version 0x04010000, u32 entry table offset (0x20), u32 entry size (0x20), u32 entry count, u32 total size}, then one
// entry per file {u32 data offset, u32 size, u32 0, u32 path hash, u32 name offset, 12 bytes 0} and NUL-terminated names. Files are written under their names in "<file>.d".
static enumError extract_addpack_file (ccp arg, ccp basedir, uint depth)
{
	u8 magic[8];
	FILE *f = fopen (arg, "rb");
	const bool hit = f && fread (magic, 1, 8, f) == 8 && !memcmp (magic, "add\0\x04\x01\x00\x00", 8);
	if (f)
		fclose (f);
	if (!hit)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = 0;
	enumError err = ERR_NOTHING_TO_DO;
	const u64 to = sz >= 0x18 ? rd_be32 (raw + 8) : 0, es = sz >= 0x18 ? rd_be32 (raw + 12) : 0, cnt = sz >= 0x18 ? rd_be32 (raw + 16) : 0;
	if (!cnt || cnt > 100000 || es < 0x14 || es > 0x100 || to < 0x18 || to + es * cnt > sz)
		goto done;
	entries = CALLOC (cnt, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	uint n = 0;
	for (u64 i = 0; i < cnt; i++)
	{
		const u8 *e = raw + to + es * i;
		const u64 off = rd_be32 (e), len = rd_be32 (e + 4), no = rd_be32 (e + 16);
		if (off > sz || len > sz - off || no >= sz)
			continue;
		char name[PATH_MAX];
		size_t nl = 0;
		while (no + nl < sz && raw[no + nl] && nl < sizeof (name) - 1)
		{
			name[nl] = (char)raw[no + nl];
			nl++;
		}
		name[nl] = 0;
		if (valid_sarc_path (name) && OwnedEntryAdd (entries, n, name, raw + off, (uint)len))
			n++;
	}
	if (n)
	{
		err = write_owned_entries (arg, basedir, depth, "ADD", entries, n);
		entries = 0;
	}
done:
	if (entries)
		ResetOwnedEntries (entries, cnt);
	FREE (raw);
	return err;
}
