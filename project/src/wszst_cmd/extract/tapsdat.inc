// Pet Vet / Paws & Claws style ".dat" packages (Marine Patrol "wii/*.dat", magic text such as "TAPSeaWii" or "Tier..." in
// the first 0x20 bytes), big-endian: u32 entry count @0x20, u32 0x10000 @0x24, entries of 0x80 bytes from 0x40:
// NUL padded backslash path (0x68 bytes), u32 data offset, u32 stored size, u32 flag (1 = zlib), u32 0, u32 unpacked
// size, u32 0. Members are written under their stored path with "/" separators.
static enumError extract_tapsdat_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dat"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 + 0x80 || rd_be32 (raw + 0x24) != 0x10000)
		goto done;
	const uint count = rd_be32 (raw + 0x20);
	if (!count || count > 100000 || 0x40 + 0x80 * (size_t)count > sz)
		goto done;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + 0x40 + 0x80 * (size_t)i;
		const size_t o = rd_be32 (e + 0x68), s = rd_be32 (e + 0x6c);
		const uint fl = rd_be32 (e + 0x70);
		if (!e[0] || e[0x67] || fl > 1 || o < 0x40 + 0x80 * (size_t)count || o > sz || s > sz - o)
			goto done;
		for (const u8 *c = e; *c; c++)
			if (*c < 0x20 || *c >= 0x7f)
				goto done;
	}
	nintendo_sarc_entry_t *entries = CALLOC (count, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	uint k = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + 0x40 + 0x80 * (size_t)i;
		const size_t o = rd_be32 (e + 0x68), s = rd_be32 (e + 0x6c), us = rd_be32 (e + 0x78);
		char name[0x70];
		snprintf (name, sizeof (name), "%s", (ccp)e);
		for (char *p = name; *p; p++)
			if (*p == '\\')
				*p = '/';
		const char *nm = name;
		while (*nm == '/')
			nm++;
		bool dup = false;
		for (uint j = 0; j < k; j++)
			if (!strcasecmp (entries[j].name, nm))
				dup = true;
		char alt[0x80];
		if (dup || !OwnedNameOk (nm))
		{
			snprintf (alt, sizeof (alt), "%04u.bin", i);
			nm = alt;
		}
		bool good = false;
		if (rd_be32 (e + 0x70) == 1)
		{
			uLongf dl = us;
			u8 *body = us < (512u << 20) ? MALLOC (us ? us : 1) : 0;
			if (body && uncompress (body, &dl, raw + o, (uLong)s) == Z_OK && dl == us)
				good = OwnedEntryAdd (entries, k, nm, body, (uint)dl);
			FREE (body);
		}
		else
			good = OwnedEntryAdd (entries, k, nm, raw + o, (uint)s);
		if (good)
			k++;
	}
	if (k)
		err = write_owned_entries (arg, basedir, depth, "TAPSDAT", entries, k);
	else
		ResetOwnedEntries (entries, count);
done:
	FREE (raw);
	return err;
}
