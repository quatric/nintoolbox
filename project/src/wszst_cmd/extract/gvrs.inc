// Sonic Team ".GVRs" texture sets (Sonic and the Secret Rings "Now/*.GVRs"). Big-endian: u16 count, u16 flag
// (1 = no names), count x u32 offset, then (flag 0) count NUL-terminated names; every member is a complete GVR
// texture ("GCIX" / "GBIX" + "GVRT", whose LE u32 at +0x14 gives the payload size). Written as "<name>.gvr"
// (the GVR handler decodes them to PNG).
static enumError extract_gvrs_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".gvrs"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 8 ? (raw[0] << 8 | raw[1]) : 0, flag = sz >= 8 ? (raw[2] << 8 | raw[3]) : 0;
	if (!count || count > 4096 || flag > 1 || 4 + 4 * (size_t)count > sz)
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
	size_t np = 4 + 4 * (size_t)count; // name table
	uint n = 0;
	bool ok = true;
	for (uint i = 0; i < count && ok; i++)
	{
		const size_t off = rd_be32 (raw + 4 + 4 * i);
		if (off + 0x20 > sz || (memcmp (raw + off, "GCIX", 4) && memcmp (raw + off, "GBIX", 4))
			|| memcmp (raw + off + 0x10, "GVRT", 4))
		{
			ok = false;
			break;
		}
		size_t len = 0x18 + (size_t)raw[off + 0x14] + ((size_t)raw[off + 0x15] << 8)
			+ ((size_t)raw[off + 0x16] << 16) + ((size_t)raw[off + 0x17] << 24);
		if (len > sz - off)
			len = sz - off;
		char base[0x41] = "", name[0x60];
		if (!flag)
		{
			size_t e = np;
			while (e < sz && raw[e])
				e++;
			if (e - np > 0x40)
				e = np + 0x40;
			memcpy (base, raw + np, e - np);
			base[e - np] = 0;
			np = e + 1;
		}
		for (char *p = base; *p; p++)
			if (*p == '/' || *p == '\\' || (u8)*p < 0x20)
				*p = '_';
		if (!*base)
			snprintf (base, sizeof (base), "tex%03u", i);
		snprintf (name, sizeof (name), "%s.gvr", base);
		for (uint dup = 1, k = 0; k < n; k++)
			if (!strcmp (entries[k].name, name))
				snprintf (name, sizeof (name), "%s-%u.gvr", base, dup++), k = (uint)-1;
		ok = OwnedEntryAdd (entries, n, name, raw + off, (uint)len);
		if (ok)
			n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && n)
		err = write_owned_entries (arg, basedir, depth, "GVRS", entries, n);
	else
		ResetOwnedEntries (entries, n);
	FREE (raw);
	return err;
}
