// Sega texture set ".gts" (Ghost Squad, Wii): u32 total size, "gts1", u16 count @8, u16 0, then u32 offsets
// from 0xc (unused slots 0xffffffff). Each entry is {u32 GVR size, char name[0x3c]} followed at +0x40 by a
// complete GVR texture ("GCIX" / "GVRT"). Members are written as "<name>.gvr" (decoded to PNG by the GVR
// handler).
static enumError extract_gts_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".gts"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 0x10 ? rd_be16 (raw + 8) : 0;
	if (sz < 0x50 || memcmp (raw + 4, "gts1", 4) || !count || count > 256 || 0xc + 4 * (size_t)count > sz)
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
		const u32 off = rd_be32 (raw + 0xc + 4 * i);
		if (off == 0xffffffff)
			continue;
		const size_t gsz = off <= sz && sz - off >= 0x40 ? rd_be32 (raw + off) : 0;
		if (!gsz || (size_t)off + 0x40 + gsz > sz || (memcmp (raw + off + 0x40, "GCIX", 4) && memcmp (raw + off + 0x40, "GVRT", 4)))
		{
			ok = false;
			break;
		}
		char base[0x3d], name[0x60];
		memcpy (base, raw + off + 4, 0x3c);
		base[0x3c] = 0;
		for (char *p = base; *p; p++)
			if (*p == '/' || *p == '\\' || (u8)*p < 0x20)
				*p = '_';
		if (!*base)
			snprintf (base, sizeof (base), "tex%03u", i);
		snprintf (name, sizeof (name), "%s.gvr", base);
		for (uint dup = 1, k = 0; k < n; k++)
			if (!strcmp (entries[k].name, name))
				snprintf (name, sizeof (name), "%s-%u.gvr", base, dup++), k = (uint)-1;
		ok = OwnedEntryAdd (entries, n, name, raw + off + 0x40, gsz);
		if (ok)
			n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && n)
		err = write_owned_entries (arg, basedir, depth, "GTS", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
