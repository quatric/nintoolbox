// "IGEL*epk" packs (Stunt Cars, WiiWare: "game.epk"), little-endian: {char magic[8] "IGEL*epk", u32 version word, u32 end of the entries, u32 entry count}, then the entries, each {u32 unpacked size, u32 stored size, u32 path hash,
// u32 flags} followed by one raw deflate stream. A directory of {u32 0x4a577309, u32 path hash, u32 unpacked size, u32 stored size + 16, u32 flags 0x200018, u32 entry offset, backslash path in a padded field} per entry
// starts at the end offset (the low 16 bits of the flags word give the length of the padded path field). Entries are written under their paths in "<file>.d" (scripts, NDP/BLOC/VEHI data, NDS audio, JPEGs; the contents are not decoded). An entry whose deflate stream does not inflate to its unpacked size is skipped.
static enumError extract_igelepk_file (ccp arg, ccp basedir, uint depth)
{
	u8 magic[8];
	FILE *f = fopen (arg, "rb");
	const bool hit = f && fread (magic, 1, 8, f) == 8 && !memcmp (magic, "IGEL*epk", 8);
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
	const u64 end = sz >= 0x14 ? rd_le32 (raw + 0x0c) : 0, cnt = sz >= 0x14 ? rd_le32 (raw + 0x10) : 0;
	if (!cnt || cnt > 1000000 || end < 0x14 || end > sz)
		goto done;
	entries = CALLOC (cnt, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	uint n = 0;
	u64 p = end;
	for (u64 i = 0; i < cnt && p + 24 <= sz; i++)
	{
		const u64 us = rd_le32 (raw + p + 8), off = rd_le32 (raw + p + 20), nl = rd_le32 (raw + p + 16) & 0xffff;
		if (!nl || nl >= PATH_MAX || nl > sz - p - 24)
			break;
		char name[PATH_MAX];
		memcpy (name, raw + p + 24, nl);
		name[nl] = 0; // a path may fill its field without a NUL
		for (char *q = name; *q; q++)
			if (*q == '\\')
				*q = '/';
		p += 24 + nl; // the low half of the flags word is the length of the padded path field
		if (off + 16 > sz || !valid_sarc_path (name) || us > 0x40000000)
			continue;
		const u64 eus = rd_le32 (raw + off), ecs = rd_le32 (raw + off + 4);
		if (eus != us || ecs > sz - off - 16)
			continue;
		u8 *buf = MALLOC (us ? us : 1);
		if (!buf)
		{
			err = ERR_OUT_OF_MEMORY;
			goto done;
		}
		bool ok = false;
		z_stream z;
		memset (&z, 0, sizeof (z));
		if (inflateInit2 (&z, -15) == Z_OK)
		{
			z.next_in = raw + off + 16;
			z.avail_in = ecs;
			z.next_out = buf;
			z.avail_out = us ? us : 1;
			ok = inflate (&z, Z_FINISH) == Z_STREAM_END && z.total_out == us;
			inflateEnd (&z);
		}
		if (ok && OwnedEntryAdd (entries, n, name, buf, (uint)us))
			n++;
		FREE (buf);
	}
	if (n)
	{
		err = write_owned_entries (arg, basedir, depth, "EPK", entries, n);
		entries = 0;
	}
done:
	if (entries)
		ResetOwnedEntries (entries, cnt);
	FREE (raw);
	return err;
}
