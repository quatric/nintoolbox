// Pikmin 1 ".anm" animation bundles (StdSystem::loadBundle in the decompilation): {s32 count}, then per entry
// {s32 type (2 = DCA, 3 = DCK), s32 size, s32 name length, name, size bytes}. Entries are written under their stored
// names (".dca" / ".dck" motion data, not decoded further).
static enumError extract_pikmin_anm_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".anm"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 16 ? rd_be32 (raw) : 0;
	if (!count || count > 4096)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// validate the whole chain first: it must end exactly at EOF
	size_t p = 4;
	for (uint i = 0; i < count; i++)
	{
		if (p + 12 > sz)
			p = sz + 1;
		else
		{
			const uint type = rd_be32 (raw + p), size = rd_be32 (raw + p + 4), nl = rd_be32 (raw + p + 8);
			if (type > 3 || nl == 0 || nl > 256 || p + 12 + (size_t)nl + size > sz)
				p = sz + 1;
			else
				p += 12 + (size_t)nl + size;
		}
		if (p > sz)
			break;
	}
	if (p != sz)
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
	p = 4;
	for (uint i = 0; i < count; i++)
	{
		const uint size = rd_be32 (raw + p + 4), nl = rd_be32 (raw + p + 8);
		char name[300];
		snprintf (name, sizeof (name), "%.*s", (int)nl, (ccp)raw + p + 12);
		for (char *q = name; *q; q++)
			if (*q == '/' || *q == '\\')
				*q = '_';
		if (!name[0])
			snprintf (name, sizeof (name), "%04u.bin", i);
		if (OwnedEntryAdd (entries, n, name, raw + p + 12 + nl, size))
			n++;
		p += 12 + (size_t)nl + size;
	}
	FREE (raw);
	if (!n)
	{
		FREE (entries);
		return ERR_NOTHING_TO_DO;
	}
	return write_owned_entries (arg, basedir, depth, "PIKMIN-ANM", entries, n);
}
