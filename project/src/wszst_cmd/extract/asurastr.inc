// Rebellion Asura engine string table (".str": PDC World Championship Darts, Wii). All fields
// little-endian: u32 count, then count u32 offsets (relative to the end of the offset table), each
// pointing at {u16 byte length, u16 character count, UTF-8 text, 0xff padding to 4 bytes}. "<name>.txt"
// lists one "<index>: <text>" line per string with backslash, newline and carriage return escaped.
static enumError extract_asurastr_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".str"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 count = sz >= 8 ? rd_le32 (raw) : 0;
	if (!count || count > 0x10000 || 4 + (size_t)count * 4 > sz || rd_le32 (raw + 4))
		goto bad;
	const size_t base = 4 + (size_t)count * 4;
	for (u32 i = 0; i < count; i++)
	{
		const size_t p = base + rd_le32 (raw + 4 + i * 4);
		if (p < base || p > sz || sz - p < 4)
			goto bad;
		const uint len = rd_le16 (raw + p), chars = rd_le16 (raw + p + 2);
		if (chars > len || len > sz - p - 4)
			goto bad;
	}
	{
		size_t cap = 4096, used = 0;
		char *txt = MALLOC (cap);
		if (!txt)
		{
			FREE (raw);
			return ERR_OUT_OF_MEMORY;
		}
		for (u32 i = 0; i < count; i++)
		{
			const size_t p = base + rd_le32 (raw + 4 + i * 4);
			const uint len = rd_le16 (raw + p);
			if (used + 16 + 2 * (size_t)len > cap)
			{
				cap = 2 * cap + 16 + 2 * (size_t)len;
				char *t = REALLOC (txt, cap);
				if (!t)
				{
					FREE (txt);
					FREE (raw);
					return ERR_OUT_OF_MEMORY;
				}
				txt = t;
			}
			used += snprintf (txt + used, cap - used, "%u: ", i);
			for (uint k = 0; k < len; k++)
			{
				const u8 c = raw[p + 4 + k];
				if (c == '\\' || c == '\n' || c == '\r')
				{
					txt[used++] = '\\';
					txt[used++] = c == '\\' ? '\\' : c == '\n' ? 'n' : 'r';
				}
				else
					txt[used++] = c;
			}
			txt[used++] = '\n';
		}
		ccp b = strrchr (arg, '/');
		b = b ? b + 1 : arg;
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s", b);
		char *dot = strrchr (name, '.');
		if (dot)
			*dot = 0;
		snprintf (name + strlen (name), sizeof (name) - strlen (name), ".txt");
		nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
		if (!entries || !OwnedEntryAdd (entries, 0, name, (u8 *)txt, used))
		{
			FREE (entries);
			FREE (txt);
			FREE (raw);
			return ERR_OUT_OF_MEMORY;
		}
		FREE (txt);
		FREE (raw);
		return write_owned_entries (arg, basedir, depth, "ASURA-STR", entries, 1);
	}
bad:
	FREE (raw);
	return ERR_NOTHING_TO_DO;
}
