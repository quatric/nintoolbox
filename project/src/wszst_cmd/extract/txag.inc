// Sonic Team "TXAG" texture archive (Sonic and the Black Knight ".txd"). Big-endian: "TXAG", u32 count, then
// count 0x28-byte entries {u32 offset, u32 size, char name[0x20]} followed by the members, each a complete
// GVR texture ("GCIX" / "GBIX" + "GVRT"). Written as "<name>.gvr" (decoded to PNG by the GVR handler).
static enumError extract_txag_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".txd"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 8 ? rd_be32 (raw + 4) : 0;
	if (memcmp (raw, "TXAG", 4) || !count || count > 4096 || 8 + 0x28 * (size_t)count > sz)
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
	bool ok = true;
	for (uint i = 0; i < count && ok; i++)
	{
		const u8 *e = raw + 8 + 0x28 * i;
		const size_t off = rd_be32 (e), sn = rd_be32 (e + 4);
		if (off > sz || sn > sz - off)
		{
			ok = false;
			break;
		}
		char base[0x21], name[0x40];
		memcpy (base, e + 8, 0x20);
		base[0x20] = 0;
		for (char *p = base; *p; p++)
			if (*p == '/' || *p == '\\' || (u8)*p < 0x20)
				*p = '_';
		if (!*base)
			snprintf (base, sizeof (base), "tex%03u", i);
		snprintf (name, sizeof (name), "%s.gvr", base);
		for (uint dup = 1, k = 0; k < n; k++)
			if (!strcmp (entries[k].name, name))
				snprintf (name, sizeof (name), "%s-%u.gvr", base, dup++), k = (uint)-1;
		ok = OwnedEntryAdd (entries, n, name, raw + off, (uint)sn);
		if (ok)
			n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && n)
		err = write_owned_entries (arg, basedir, depth, "TXAG", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
