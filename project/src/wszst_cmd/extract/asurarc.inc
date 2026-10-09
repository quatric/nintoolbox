// Rebellion Asura engine "wii\0" resource archive (".ARC": PDC World Championship Darts, Wii; engine
// builds on other platforms use other headers). All fields big-endian:
//   +0x00 "wii\0", +0x04 file size, +0x08 entry count, then 0xd4 bytes of header (the archive name
//   three times, 0xbaadf00d padded), followed by count 0x5c-byte entries
//   {char name[0x40], u32 type, u32 size, u32 offset, u32 index, u32 0, u32 align, u32 0}.
// Type 0x15 members are standard DSP-ADPCM files (header + data), type 0x14 nested U8 archives.
// Members are written as "<name>.dsp", "<name>.arc" or "<name>.t<type>" ("-N" appended on repeats).
static enumError extract_asurarc_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 0xd4 ? rd_be32 (raw + 8) : 0;
	if (sz < 0x100 || memcmp (raw, "wii\0", 4) || rd_be32 (raw + 4) != sz || !count || count > 20000
		|| 0xd4 + (size_t)count * 0x5c > sz)
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
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + 0xd4 + (size_t)i * 0x5c;
		const u32 type = rd_be32 (e + 0x40), size = rd_be32 (e + 0x44), off = rd_be32 (e + 0x48);
		if ((size_t)off + size > sz)
			break;
		char base[0x41];
		memcpy (base, e, 0x40);
		base[0x40] = 0;
		for (char *p = base; *p; p++)
			if (*p == '/' || *p == '\\' || (u8)*p < 0x20)
				*p = '_';
		if (!*base)
			snprintf (base, sizeof (base), "entry%04u", i);
		const char *ext = type == 0x15 ? "dsp" : type == 0x14 && size > 4 && !memcmp (raw + off, "\x55\xaa\x38\x2d", 4) ? "arc" : 0;
		char name[0x60];
		uint dup = 0;
		for (;;)
		{
			if (ext)
				snprintf (name, sizeof (name), dup ? "%s-%u.%s" : "%s%.0u.%s", base, dup, ext);
			else
				snprintf (name, sizeof (name), dup ? "%s-%u.t%u" : "%s%.0u.t%u", base, dup, type);
			uint k = 0;
			while (k < n && strcmp (entries[k].name, name))
				k++;
			if (k == n)
				break;
			dup++;
		}
		if (!OwnedEntryAdd (entries, n, name, size ? raw + off : (const u8 *)"", size))
			break;
		n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (n == count)
		err = write_owned_entries (arg, basedir, depth, "ASURA-ARC", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
