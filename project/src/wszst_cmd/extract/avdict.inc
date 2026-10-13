// Avalanche Software "DICT" localisation dictionary (".dct": Disney's Bolt, Wii): "DICT", u32 0x2000, u32
// hash, u32 version, then hash tables followed by a pool of NUL-terminated UTF-8 strings (each text is
// followed by its voice / string id). The table layout is not decoded; the pool is located by scanning
// back from the end of the file to the last run of three NUL bytes (the table end) and written as
// "<name>.txt" with one "<index>: <text>" line per string (backslash, newline and carriage return escaped).
static enumError extract_avdict_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dct"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x100 || memcmp (raw, "DICT", 4) || rd_le32 (raw + 4) != 0x2000 || raw[sz - 1])
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t start = sz;
	while (start > 3 && (raw[start - 1] || raw[start - 2] || raw[start - 3]))
		start--;
	char *txt = MALLOC (2 * (sz - start) + 32 * ((sz - start) / 2 + 1) + 64);
	if (!txt)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	size_t used = 0;
	uint idx = 0;
	for (size_t p = start; p < sz;)
	{
		used += snprintf (txt + used, 32, "%u: ", idx++);
		for (; p < sz && raw[p]; p++)
		{
			const u8 c = raw[p];
			if (c == '\\' || c == '\n' || c == '\r')
			{
				txt[used++] = '\\';
				txt[used++] = c == '\\' ? '\\' : c == '\n' ? 'n' : 'r';
			}
			else
				txt[used++] = c;
		}
		p++;
		txt[used++] = '\n';
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (idx >= 16)
	{
		ccp b = strrchr (arg, '/');
		b = b ? b + 1 : arg;
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s", b);
		char *dot = strrchr (name, '.');
		if (dot)
			*dot = 0;
		snprintf (name + strlen (name), sizeof (name) - strlen (name), ".txt");
		nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
		if (entries && OwnedEntryAdd (entries, 0, name, (u8 *)txt, used))
			err = write_owned_entries (arg, basedir, depth, "AV-DICT", entries, 1);
		else
		{
			FREE (entries);
			err = ERR_OUT_OF_MEMORY;
		}
	}
	FREE (txt);
	FREE (raw);
	return err;
}
